// Phase 2C Benchmark v1
// Measures: decode FPS, CPU end-to-end FPS, YUV end-to-end FPS, first-frame latency.
// Minimal: no custom profiler, no GL harness, no system info.
// Run: build\LunarPlayerBenchmark.exe (requires native display)

#include <QApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QImage>
#include <cstdio>
#include <cmath>

#include "ui/MainWindow.h"
#include "ui/VideoWidget.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

// ---- Minimal decode session (decompressor only) ----
class DecodeSession {
public:
    ~DecodeSession() { close(); }
    bool open(const QString &path);
    void close();
    bool readFrame(); // returns true while frames available
    int decodeAll();  // decode all frames, return count
    double durationSec() const { return m_dur; }
    int fps() const { return m_fps; }
    int width() const { return m_w; }
    int height() const { return m_h; }
private:
    AVFormatContext *fmt = nullptr;
    AVCodecContext *codec = nullptr;
    const AVCodec *dec = nullptr;
    int vi = -1, m_w = 0, m_h = 0;
    double m_dur = 0; int m_fps = 24;
    AVPacket *pkt = nullptr;
    AVFrame *frame = nullptr;
};

bool DecodeSession::open(const QString &path) {
    close();
    QByteArray pb = path.toUtf8();
    if (avformat_open_input(&fmt, pb.constData(), nullptr, nullptr) != 0) return false;
    if (avformat_find_stream_info(fmt, nullptr) < 0) return false;
    vi = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (vi < 0) return false;
    codec = avcodec_alloc_context3(dec);
    if (!codec) return false;
    avcodec_parameters_to_context(codec, fmt->streams[vi]->codecpar);
    if (avcodec_open2(codec, dec, nullptr) < 0) return false;
    m_w = codec->width; m_h = codec->height;
    double dur = (double)fmt->duration / AV_TIME_BASE;
    if (dur <= 0) {
        AVRational tb = fmt->streams[vi]->time_base;
        dur = (double)fmt->streams[vi]->duration * tb.num / tb.den;
    }
    m_dur = dur;
    AVRational fr = fmt->streams[vi]->avg_frame_rate;
    m_fps = (fr.den > 0) ? qRound((double)fr.num / fr.den) : 24;
    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    return true;
}

void DecodeSession::close() {
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codec);
    avformat_close_input(&fmt);
}

bool DecodeSession::readFrame() {
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == vi) {
            if (avcodec_send_packet(codec, pkt) == 0) {
                int ret = avcodec_receive_frame(codec, frame);
                if (ret == 0) { av_packet_unref(pkt); return true; }
            }
        }
        av_packet_unref(pkt);
    }
    return false;
}

int DecodeSession::decodeAll() {
    int n = 0;
    while (readFrame()) n++;
    return n;
}

// ---- Helpers ----
static double measureDecodeFPS(const QString &path, int *outFrames = nullptr) {
    DecodeSession s;
    if (!s.open(path)) return 0;
    QElapsedTimer t; t.start();
    int n = s.decodeAll();
    double sec = t.elapsed() / 1000.0;
    if (outFrames) *outFrames = n;
    return (sec > 0) ? n / sec : 0;
}

// CPU end-to-end: decode + sws_scale (full software pipeline)
// This measures decode + YUV→RGB conversion throughput (no GPU involvement).
static double measureCPUE2E(const QString &path, bool countOnly = false) {
    // Reuse MediaSession's readFrame which does decode + sws_scale internally.
    // This is the "could this file play back in software at this framerate" number.
    MediaSession ms;
    if (!ms.open(path)) return 0;
    int n = 0;
    QElapsedTimer t; t.start();
    while (ms.readFrame()) n++;
    double sec = t.elapsed() / 1000.0;
    ms.close();
    return (sec > 0) ? n / sec : 0;
}

// YUV end-to-end: decode + YUV render through MainWindow (GPU pipeline).
// This measures full application-level playback throughput.
static double measureYUVE2E(const QString &path, int *outFrames = nullptr) {
    MainWindow mw;
    mw.resize(640, 480);
    mw.show();
    for (int i = 0; i < 10; i++) QApplication::processEvents();
    if (!mw.session()->open(path)) return 0;
    int n = 0;
    QElapsedTimer t; t.start();
    while (mw.session()->readFrame()) {
        mw.applyCurrentFrame();
        QApplication::processEvents();
        n++;
    }
    double sec = t.elapsed() / 1000.0;
    mw.session()->close();
    if (outFrames) *outFrames = n;
    return (sec > 0) ? n / sec : 0;
}

// ---- Main ----
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Lunar Player Benchmark v1");

    QString base = QDir::cleanPath(
        QApplication::applicationDirPath() + "/../../tests/media");
    if (!QDir(base).exists())
        base = QDir::cleanPath(
            QString::fromLocal8Bit(TEST_SOURCE_DIR) + "/tests/media");

    struct Test { QString file; QString label; };
    Test tests[] = {
        {"qa_1080p_h264_10s.mp4", "1080p H264"},
        {"qa_1080p_h265_10s.mp4", "1080p H265"},
        {"qa_4k_h264_5s.mp4",     "4K H264"},
        {"qa_long_480p_60s.mp4",  "480p long"},
    };

    printf("\n");
    printf("=== Phase 2C Benchmark v1 ===\n\n");

    // Header
    printf("%-15s %10s %10s %10s %10s %10s\n",
           "File", "Decode", "CPU_E2E", "YUV_E2E", "FF_Lat", "Speedup");
    printf("%-15s %10s %10s %10s %10s %10s\n",
           "", "FPS", "FPS", "FPS", "ms", "");
    printf("%-15s %10s %10s %10s %10s %10s\n",
           "---------------", "-----", "-----", "-----", "-----", "-----");

    for (auto &t : tests) {
        QString full = QDir(base).filePath(t.file);
        if (!QFile::exists(full)) {
            printf("%-15s %10s\n", qPrintable(t.label), "MISSING");
            continue;
        }

        printf("%-15s", qPrintable(t.label));
        fflush(stdout);

        // Decode FPS
        double decFPS = measureDecodeFPS(full);
        printf(" %10.1f", decFPS);
        fflush(stdout);

        // CPU end-to-end (decode + sws)
        double cpuE2E = measureCPUE2E(full);
        printf(" %10.1f", cpuE2E);
        fflush(stdout);

        // YUV end-to-end (decode + GPU render)
        double yuvE2E = measureYUVE2E(full);
        printf(" %10.1f", yuvE2E);
        fflush(stdout);

        // First-frame latency (open + decode first frame + render)
        {
            MainWindow mw;
            mw.resize(640, 480);
            mw.show();
            for (int i = 0; i < 5; i++) QApplication::processEvents();
            QElapsedTimer t; t.start();
            if (mw.session()->open(full)) {
                mw.session()->readFrame();
                mw.applyCurrentFrame();
                for (int i = 0; i < 5; i++) QApplication::processEvents();
            }
            printf(" %10.0f", (double)t.elapsed());
            fflush(stdout);
        }

        // Speedup
        double ratio = (cpuE2E > 0 && yuvE2E > 0) ? yuvE2E / cpuE2E : 0;
        printf(" %10.2fx\n", ratio);
        fflush(stdout);
    }

    printf("\nDone.\n");
    return 0;
}
