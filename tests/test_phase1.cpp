#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <cstring>
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

// A simple FFmpeg-based media session (same logic as MainWindow's MediaSession)
// but isolated for headless testing without Qt GUI dependencies.
class TestSession {
public:
    TestSession() = default;
    ~TestSession() { close(); }

    bool open(const QString &path) {
        close();
        QByteArray pathBytes = path.toUtf8();

        if (avformat_open_input(&m_fmtCtx, pathBytes.constData(), nullptr, nullptr) != 0)
            return false;
        if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0) {
            avformat_close_input(&m_fmtCtx);
            return false;
        }

        m_videoStreamIdx = av_find_best_stream(m_fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &m_codec, 0);
        if (m_videoStreamIdx < 0) {
            avformat_close_input(&m_fmtCtx);
            return false;
        }

        m_codecCtx = avcodec_alloc_context3(m_codec);
        if (!m_codecCtx) {
            avformat_close_input(&m_fmtCtx);
            return false;
        }
        avcodec_parameters_to_context(m_codecCtx, m_fmtCtx->streams[m_videoStreamIdx]->codecpar);

        if (avcodec_open2(m_codecCtx, m_codec, nullptr) < 0) {
            avcodec_free_context(&m_codecCtx);
            avformat_close_input(&m_fmtCtx);
            return false;
        }

        m_width = m_codecCtx->width;
        m_height = m_codecCtx->height;

        double dur = static_cast<double>(m_fmtCtx->duration) / AV_TIME_BASE;
        if (dur <= 0) {
            AVRational tb = m_fmtCtx->streams[m_videoStreamIdx]->time_base;
            dur = static_cast<double>(m_fmtCtx->streams[m_videoStreamIdx]->duration) * tb.num / tb.den;
        }
        m_durationSec = dur;

        AVRational avg_framerate = m_fmtCtx->streams[m_videoStreamIdx]->avg_frame_rate;
        if (avg_framerate.den > 0)
            m_fps = qRound(static_cast<double>(avg_framerate.num) / avg_framerate.den);
        else
            m_fps = 24;

        m_swsCtx = sws_getContext(m_width, m_height, m_codecCtx->pix_fmt,
                                   m_width, m_height, AV_PIX_FMT_RGB24,
                                   SWS_BILINEAR, nullptr, nullptr, nullptr);
        m_pkt = av_packet_alloc();
        m_decodedFrame = av_frame_alloc();
        return true;
    }

    void close() {
        av_frame_free(&m_decodedFrame);
        av_packet_free(&m_pkt);
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
        avcodec_free_context(&m_codecCtx);
        avformat_close_input(&m_fmtCtx);
        m_videoStreamIdx = -1;
        m_width = m_height = 0;
        m_durationSec = 0.0;
        m_fps = 24;
        m_lastFrame = QImage();
        m_frameCount = 0;
    }

    bool readFrame() {
        if (!m_fmtCtx) return false;
        while (av_read_frame(m_fmtCtx, m_pkt) >= 0) {
            if (m_pkt->stream_index == m_videoStreamIdx) {
                if (avcodec_send_packet(m_codecCtx, m_pkt) == 0) {
                    int ret = avcodec_receive_frame(m_codecCtx, m_decodedFrame);
                    if (ret == 0) {
                        QImage img(m_width, m_height, QImage::Format_RGB888);
                        uint8_t *dstData[1] = { img.bits() };
                        int dstLinesize[1] = { static_cast<int>(img.bytesPerLine()) };
                        sws_scale(m_swsCtx,
                                  m_decodedFrame->data, m_decodedFrame->linesize,
                                  0, m_height,
                                  dstData, dstLinesize);
                        m_lastFrame = img.copy();
                        m_frameCount++;
                        av_packet_unref(m_pkt);
                        return true;
                    }
                }
            }
            av_packet_unref(m_pkt);
        }
        return false;
    }

    void seekSec(double sec) {
        if (!m_fmtCtx || m_videoStreamIdx < 0) return;
        int64_t ts = static_cast<int64_t>(sec * AV_TIME_BASE);
        if (ts < 0) ts = 0;
        av_seek_frame(m_fmtCtx, -1, ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(m_codecCtx);
    }

    bool isOpen() const { return m_fmtCtx != nullptr; }
    double durationSec() const { return m_durationSec; }
    int fps() const { return m_fps; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    int frameCount() const { return m_frameCount; }
    QImage lastFrame() const { return m_lastFrame; }

private:
    AVFormatContext *m_fmtCtx = nullptr;
    AVCodecContext *m_codecCtx = nullptr;
    const AVCodec *m_codec = nullptr;
    SwsContext *m_swsCtx = nullptr;
    int m_videoStreamIdx = -1;
    int m_width = 0, m_height = 0;
    double m_durationSec = 0.0;
    int m_fps = 24;
    AVPacket *m_pkt = nullptr;
    AVFrame *m_decodedFrame = nullptr;
    QImage m_lastFrame;
    int m_frameCount = 0;
};

class TestPhase1Integration : public QObject {
    Q_OBJECT

private:
    QString m_testVideo;
    QString m_nonexistentVideo;

    // Compute average pixel value to verify frame content changed
    double avgPixel(const QImage &img) {
        if (img.isNull()) return -1.0;
        double sum = 0.0;
        int count = 0;
        for (int y = 0; y < qMin(img.height(), 32); y++) {
            const uchar *line = img.constScanLine(y);
            for (int x = 0; x < qMin(img.width(), 32) * 3; x++) {
                sum += line[x];
                count++;
            }
        }
        return (count > 0) ? (sum / count) : -1.0;
    }

public:
    TestPhase1Integration() {
        QString testDir = QString::fromLocal8Bit(TEST_SOURCE_DIR) + "/tests/media";
        m_testVideo = QDir::cleanPath(testDir + "/test_bars_24fps_2s.mp4");
        m_nonexistentVideo = QDir::cleanPath(testDir + "/does_not_exist.mp4");
    }

private slots:
    void initTestCase() {
        // Verify test video exists
        QVERIFY2(QFile::exists(m_testVideo),
                 qPrintable(QString("Test video not found: %1").arg(m_testVideo)));
    }

    // ---- Requirement 1: Open MP4 ----
    void testOpenValidFile() {
        TestSession session;
        QVERIFY(session.open(m_testVideo));
        QVERIFY(session.isOpen());
        QCOMPARE(session.width(), 320);
        QCOMPARE(session.height(), 240);
        QCOMPARE(session.fps(), 24);
        QVERIFY(qAbs(session.durationSec() - 2.0) < 0.1);
    }

    void testOpenNonExistentFile() {
        TestSession session;
        QVERIFY(!session.open(m_nonexistentVideo));
        QVERIFY(!session.isOpen());
    }

    void testReopenSameFile() {
        TestSession session;
        QVERIFY(session.open(m_testVideo));
        QVERIFY(session.open(m_testVideo));  // reopen
        QVERIFY(session.isOpen());
    }

    void testDefaultState() {
        TestSession session;
        QVERIFY(!session.isOpen());
        QCOMPARE(session.durationSec(), 0.0);
        QCOMPARE(session.fps(), 24);
        QCOMPARE(session.width(), 0);
        QCOMPARE(session.height(), 0);
        QCOMPARE(session.frameCount(), 0);
        QVERIFY(session.lastFrame().isNull());
    }

    void testCloseWhileOpen() {
        TestSession session;
        QVERIFY(session.open(m_testVideo));
        QVERIFY(session.isOpen());
        session.close();
        QVERIFY(!session.isOpen());
        QCOMPARE(session.durationSec(), 0.0);
    }

    // ---- Requirement 2: Play MP4 (decode all frames) ----
    void testDecodeAllFrames() {
        TestSession session;
        QVERIFY(session.open(m_testVideo));
        // Duration = 2s, fps = 24 => ~48 frames
        int count = 0;
        while (session.readFrame()) {
            count++;
            QVERIFY(!session.lastFrame().isNull());
            QCOMPARE(session.lastFrame().width(), 320);
            QCOMPARE(session.lastFrame().height(), 240);
        }
        // Should be close to 48 frames
        QVERIFY2(count >= 45 && count <= 52,
                 qPrintable(QString("Expected ~48 frames, got %1").arg(count)));
        QCOMPARE(session.frameCount(), count);
    }

    void testFrameContentChanges() {
        TestSession session;
        QVERIFY(session.open(m_testVideo));

        // Read first frame
        QVERIFY(session.readFrame());
        QImage frame1 = session.lastFrame();
        double avg1 = avgPixel(frame1);
        QVERIFY(avg1 >= 0);

        // Read 14 frames to cross color boundary at 0.5s (12 frames = first segment)
        for (int i = 0; i < 14; i++) session.readFrame();
        QImage frame2 = session.lastFrame();
        double avg2 = avgPixel(frame2);

        // If frames are from different color segments, avg pixel should differ
        // (Red frame ~= 85 avg, Green frame ~= 128 avg)
        bool pixelChanged = qAbs(avg1 - avg2) > 5.0;
        QVERIFY2(pixelChanged, qPrintable(QString("Frame content unchanged. avg1=%1, avg2=%2").arg(avg1).arg(avg2)));
    }

    // ---- Requirement 3: Seek MP4 ----
    void testSeekToMiddle() {
        TestSession session;
        QVERIFY(session.open(m_testVideo));

        // Seek to 1.0s (blue segment in test video)
        session.seekSec(1.0);
        QVERIFY(session.readFrame());
        QImage midFrame = session.lastFrame();
        QVERIFY(!midFrame.isNull());

        // The frame should be valid with expected dimensions
        QCOMPARE(midFrame.width(), 320);
        QCOMPARE(midFrame.height(), 240);
    }

    void testSeekChangesFrameContent() {
        TestSession session;
        QVERIFY(session.open(m_testVideo));

        session.readFrame();
        double avgStart = avgPixel(session.lastFrame());

        session.seekSec(1.8);
        session.readFrame();
        double avgEnd = avgPixel(session.lastFrame());

        // Start (red frame, ~85 avg) and near-end (white frame, ~255 avg)
        // should have different average pixel values
        QVERIFY2(qAbs(avgStart - avgEnd) > 20.0,
                 qPrintable(QString("Expected different frame content. Start avg=%1, End avg=%2")
                            .arg(avgStart).arg(avgEnd)));
    }

    void testSeekToStart() {
        TestSession session;
        QVERIFY(session.open(m_testVideo));

        // Decode some frames
        for (int i = 0; i < 10; i++) session.readFrame();
        QVERIFY(session.frameCount() >= 10);

        // Seek back to start
        session.seekSec(0.0);
        session.readFrame();
        QImage frame = session.lastFrame();
        QVERIFY(!frame.isNull());
        QCOMPARE(frame.width(), 320);
        QCOMPARE(frame.height(), 240);
    }

    void testSeekToEnd() {
        TestSession session;
        QVERIFY(session.open(m_testVideo));

        // Seek to near end
        session.seekSec(1.95);
        QVERIFY(session.readFrame());
        QVERIFY(!session.lastFrame().isNull());
    }

    void testSeekPastEnd() {
        TestSession session;
        QVERIFY(session.open(m_testVideo));

        // Seek past end should not crash
        session.seekSec(10.0);
        // Should still be able to read (might get last frame or nothing)
        bool ok = session.readFrame();
        // Either we get a frame or we reach EOF gracefully - both are acceptable
        QVERIFY(session.isOpen());  // Still open, no crash
    }

    void testSeekNegative() {
        TestSession session;
        QVERIFY(session.open(m_testVideo));

        // Seek to negative should clamp to 0
        session.seekSec(-5.0);
        QVERIFY(session.readFrame());
        QVERIFY(!session.lastFrame().isNull());
    }

    // ---- Requirement 4: Pause/Resume (state management) ----
    void testReadAfterPause() {
        TestSession session;
        QVERIFY(session.open(m_testVideo));

        // Read some frames (simulating "playing")
        for (int i = 0; i < 5; i++) session.readFrame();
        int beforePause = session.frameCount();

        // "Pause" = stop calling readFrame
        // Then "resume" = call readFrame again
        QVERIFY(session.readFrame());
        QCOMPARE(session.frameCount(), beforePause + 1);
    }

    // ---- Requirement 5: Close MP4 ----
    void testCloseStopsDecode() {
        TestSession session;
        QVERIFY(session.open(m_testVideo));

        session.readFrame();
        QVERIFY(session.frameCount() >= 1);

        session.close();
        QVERIFY(!session.isOpen());

        // readFrame should return false after close
        QVERIFY(!session.readFrame());
    }

    void testCloseIdempotent() {
        TestSession session;
        session.close();
        session.close();  // Should not crash
        QVERIFY(!session.isOpen());
    }

    void testOpenCloseReopenDecode() {
        TestSession session;
        for (int round = 0; round < 3; round++) {
            QVERIFY(session.open(m_testVideo));
            QVERIFY(session.readFrame());
            QVERIFY(!session.lastFrame().isNull());
            QCOMPARE(session.width(), 320);
            QCOMPARE(session.height(), 240);
            session.close();
            QVERIFY(!session.isOpen());
        }
    }

    // ---- Edge cases ----
    void testFrameDimensionsConsistent() {
        TestSession session;
        QVERIFY(session.open(m_testVideo));

        while (session.readFrame()) {
            QCOMPARE(session.lastFrame().width(), 320);
            QCOMPARE(session.lastFrame().height(), 240);
        }
    }

    void testMultipleSeeks() {
        TestSession session;
        QVERIFY(session.open(m_testVideo));

        double positions[] = {0.0, 0.5, 1.0, 1.5, 0.2, 1.8, 0.0};
        for (double pos : positions) {
            session.seekSec(pos);
            QVERIFY2(session.readFrame(),
                     qPrintable(QString("Failed to read frame after seek to %1s").arg(pos)));
            QVERIFY(!session.lastFrame().isNull());
        }
    }
};

QTEST_MAIN(TestPhase1Integration)
#include "test_phase1.moc"
