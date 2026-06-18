#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QElapsedTimer>
#include <QDebug>
#include <cstring>
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

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
        if (m_videoStreamIdx < 0) { avformat_close_input(&m_fmtCtx); return false; }
        m_codecCtx = avcodec_alloc_context3(m_codec);
        if (!m_codecCtx) { avformat_close_input(&m_fmtCtx); return false; }
        avcodec_parameters_to_context(m_codecCtx, m_fmtCtx->streams[m_videoStreamIdx]->codecpar);
        if (avcodec_open2(m_codecCtx, m_codec, nullptr) < 0) { avcodec_free_context(&m_codecCtx); avformat_close_input(&m_fmtCtx); return false; }
        m_width = m_codecCtx->width;
        m_height = m_codecCtx->height;
        double dur = static_cast<double>(m_fmtCtx->duration) / AV_TIME_BASE;
        if (dur <= 0) {
            AVRational tb = m_fmtCtx->streams[m_videoStreamIdx]->time_base;
            dur = static_cast<double>(m_fmtCtx->streams[m_videoStreamIdx]->duration) * tb.num / tb.den;
        }
        m_durationSec = dur;
        AVRational avg_framerate = m_fmtCtx->streams[m_videoStreamIdx]->avg_frame_rate;
        if (avg_framerate.den > 0) m_fps = qRound(static_cast<double>(avg_framerate.num) / avg_framerate.den);
        else m_fps = 24;
        m_codecName = m_codec ? m_codec->name : "unknown";
        m_swsCtx = sws_getContext(m_width, m_height, m_codecCtx->pix_fmt,
                                   m_width, m_height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
        m_pkt = av_packet_alloc();
        m_decodedFrame = av_frame_alloc();
        return true;
    }

    void close() {
        av_frame_free(&m_decodedFrame);
        av_packet_free(&m_pkt);
        sws_freeContext(m_swsCtx); m_swsCtx = nullptr;
        avcodec_free_context(&m_codecCtx);
        avformat_close_input(&m_fmtCtx);
        m_videoStreamIdx = -1;
        m_width = m_height = 0; m_durationSec = 0.0; m_fps = 24;
        m_lastFrame = QImage(); m_frameCount = 0;
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
                        sws_scale(m_swsCtx, m_decodedFrame->data, m_decodedFrame->linesize, 0, m_height, dstData, dstLinesize);
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
    QString codecName() const { return m_codecName; }

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
    QString m_codecName;
};

class TestHumanQA : public QObject {
    Q_OBJECT

private:
    QString mediaDir(const QString &file) const {
        return QDir::cleanPath(QString::fromLocal8Bit(TEST_SOURCE_DIR) + "/tests/media/" + file);
    }

    double avgPixel(const QImage &img) {
        if (img.isNull()) return -1.0;
        double sum = 0.0; int count = 0;
        int step = qMax(1, img.width() / 32);
        for (int y = 0; y < img.height(); y += step) {
            const uchar *line = img.constScanLine(y);
            for (int x = 0; x < img.width() * 3; x += step * 3) {
                sum += line[x]; count++;
            }
        }
        return (count > 0) ? (sum / count) : -1.0;
    }

private slots:
    // ===== CODEC SUPPORT =====
    void testH264CodecSupport() {
        TestSession s;
        QVERIFY(s.open(mediaDir("qa_1080p_h264_10s.mp4")));
        QCOMPARE(s.width(), 1920);
        QCOMPARE(s.height(), 1080);
        QVERIFY(s.codecName().contains("h264"));
    }

    void testH265CodecSupport() {
        TestSession s;
        QVERIFY(s.open(mediaDir("qa_1080p_h265_10s.mp4")));
        QCOMPARE(s.width(), 1920);
        QCOMPARE(s.height(), 1080);
        QVERIFY(s.codecName().contains("hevc"));
    }

    void test4KResolution() {
        TestSession s;
        QVERIFY(s.open(mediaDir("qa_4k_h264_5s.mp4")));
        QCOMPARE(s.width(), 3840);
        QCOMPARE(s.height(), 2160);
    }

    // ===== DECODE PERFORMANCE =====
    void testDecodeFPS_1080p_H264() {
        TestSession s;
        QVERIFY(s.open(mediaDir("qa_1080p_h264_10s.mp4")));
        QElapsedTimer timer;
        timer.start();
        int count = 0;
        while (s.readFrame()) count++;
        double elapsed = timer.elapsed() / 1000.0;
        double fps = elapsed > 0 ? count / elapsed : 0;
        qDebug().noquote() << QString("1080p H.264: %1 frames in %2s = %3 FPS").arg(count).arg(elapsed, 0, 'f', 2).arg(fps, 0, 'f', 1);
        QVERIFY2(fps > 10, qPrintable(QString("Decode too slow: %1 FPS").arg(fps, 0, 'f', 1)));
    }

    void testDecodeFPS_1080p_H265() {
        TestSession s;
        QVERIFY(s.open(mediaDir("qa_1080p_h265_10s.mp4")));
        QElapsedTimer timer;
        timer.start();
        int count = 0;
        while (s.readFrame()) count++;
        double elapsed = timer.elapsed() / 1000.0;
        double fps = elapsed > 0 ? count / elapsed : 0;
        qDebug().noquote() << QString("1080p H.265: %1 frames in %2s = %3 FPS").arg(count).arg(elapsed, 0, 'f', 2).arg(fps, 0, 'f', 1);
        QVERIFY2(fps > 10, qPrintable(QString("Decode too slow: %1 FPS").arg(fps, 0, 'f', 1)));
    }

    void testDecodeFPS_4K_H264() {
        TestSession s;
        QVERIFY(s.open(mediaDir("qa_4k_h264_5s.mp4")));
        QElapsedTimer timer;
        timer.start();
        int count = 0;
        while (s.readFrame()) count++;
        double elapsed = timer.elapsed() / 1000.0;
        double fps = elapsed > 0 ? count / elapsed : 0;
        qDebug().noquote() << QString("4K H.264: %1 frames in %2s = %3 FPS").arg(count).arg(elapsed, 0, 'f', 2).arg(fps, 0, 'f', 1);
        QVERIFY2(fps >= 5, qPrintable(QString("4K decode too slow: %1 FPS").arg(fps, 0, 'f', 1)));
    }

    // ===== LONG SEEK STRESS =====
    void testLongSeekJumps() {
        TestSession s;
        QVERIFY(s.open(mediaDir("qa_long_480p_60s.mp4")));

        // Seek to multiple scattered positions across the full 60s duration
        double positions[] = {0.0, 5.0, 15.0, 30.0, 45.0, 55.0, 59.0, 10.0, 50.0, 25.0};
        for (double pos : positions) {
            s.seekSec(pos);
            QVERIFY2(s.readFrame(),
                     qPrintable(QString("Failed to decode after seek to %1s").arg(pos)));
            QVERIFY(!s.lastFrame().isNull());
        }
    }

    void testSeekAndVerifyPositionChange() {
        TestSession s;
        QVERIFY(s.open(mediaDir("qa_long_480p_60s.mp4")));

        s.seekSec(0.0); s.readFrame();
        double avgStart = avgPixel(s.lastFrame());

        s.seekSec(30.0); s.readFrame();
        double avgMid = avgPixel(s.lastFrame());

        s.seekSec(59.0); s.readFrame();
        double avgEnd = avgPixel(s.lastFrame());

        // The testsrc pattern generates moving patterns, so different positions
        // should yield different average pixel values
        bool changed1 = qAbs(avgStart - avgMid) > 2.0;
        bool changed2 = qAbs(avgMid - avgEnd) > 2.0;
        QVERIFY2(changed1 || changed2,
                 qPrintable(QString("All seek positions gave same average pixels: start=%1 mid=%2 end=%3")
                            .arg(avgStart, 0, 'f', 1).arg(avgMid, 0, 'f', 1).arg(avgEnd, 0, 'f', 1)));
    }

    // ===== REPEATED PAUSE/PLAY STRESS =====
    void testRepeatedPausePlay() {
        TestSession s;
        QVERIFY(s.open(mediaDir("qa_1080p_h264_10s.mp4")));

        for (int i = 0; i < 100; i++) {
            // "Play" = read frames
            for (int j = 0; j < 5; j++) {
                bool ok = s.readFrame();
                if (!ok) {
                    // Loop back to start
                    s.seekSec(0.0);
                    s.readFrame();
                }
                QVERIFY(!s.lastFrame().isNull());
            }
            // "Pause" = just verify current frame is valid
            QVERIFY(!s.lastFrame().isNull());
            QCOMPARE(s.lastFrame().width(), 1920);
            QCOMPARE(s.lastFrame().height(), 1080);
        }
    }

    // ===== OPEN/CLOSE STRESS =====
    void testRepeatedOpenClose() {
        QStringList files = {
            mediaDir("qa_1080p_h264_10s.mp4"),
            mediaDir("qa_1080p_h265_10s.mp4"),
            mediaDir("qa_4k_h264_5s.mp4"),
            mediaDir("qa_long_480p_60s.mp4"),
            mediaDir("test_bars_24fps_2s.mp4")
        };

        for (int round = 0; round < 50; round++) {
            for (const QString &file : files) {
                TestSession s;
                QVERIFY2(s.open(file), qPrintable(QString("Round %1: Failed to open %2").arg(round).arg(file)));
                QVERIFY(s.readFrame());
                QVERIFY(!s.lastFrame().isNull());
                s.close();
                QVERIFY(!s.isOpen());
            }
        }
    }

    // ===== MEMORY USAGE ESTIMATE =====
    void testDecodeMultipleResolutions() {
        // Decode all frames from each resolution file to detect memory issues
        QVector<QPair<QString, QPair<int,int>>> specs = {
            {"qa_1080p_h264_10s.mp4", {1920, 1080}},
            {"qa_1080p_h265_10s.mp4", {1920, 1080}},
            {"qa_4k_h264_5s.mp4", {3840, 2160}},
        };

        for (const auto &spec : specs) {
            TestSession s;
            QVERIFY(s.open(mediaDir(spec.first)));
            QCOMPARE(s.width(), spec.second.first);
            QCOMPARE(s.height(), spec.second.second);

            int count = 0;
            while (s.readFrame()) {
                count++;
                QCOMPARE(s.lastFrame().width(), spec.second.first);
                QCOMPARE(s.lastFrame().height(), spec.second.second);
            }
            qDebug().noquote() << QString("%1: %2 frames decoded, %3x%4")
                .arg(spec.first).arg(count).arg(spec.second.first).arg(spec.second.second);
        }
    }

    // ===== EDGE CASES =====
    void testRepeatedSameFrame() {
        // Open same file 10 times simultaneously (different sessions)
        QVector<TestSession*> sessions;
        for (int i = 0; i < 10; i++) {
            auto *s = new TestSession();
            QVERIFY(s->open(mediaDir("test_bars_24fps_2s.mp4")));
            sessions.append(s);
        }
        for (auto *s : sessions) {
            QVERIFY(s->readFrame());
            QVERIFY(!s->lastFrame().isNull());
        }
        for (auto *s : sessions) {
            s->close();
            delete s;
        }
    }

    void testDecodeAfterExhaustion() {
        TestSession s;
        QVERIFY(s.open(mediaDir("test_bars_24fps_2s.mp4")));
        while (s.readFrame()) { /* exhaust */ }
        // After exhaustion, readFrame should return false, not crash
        QVERIFY(!s.readFrame());
        // Re-seeking should work
        s.seekSec(0.0);
        QVERIFY(s.readFrame());
        QVERIFY(!s.lastFrame().isNull());
    }
};

QTEST_MAIN(TestHumanQA)
#include "test_human_qa.moc"
