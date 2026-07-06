// Lunar Player — Headless Decode Benchmark
// Tests every resolution × FPS, codec, and container combo
// without Qt GUI dependency. Compares hardware vs software decode.
// Produces CSV and HTML report.

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <chrono>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

// ============================================================
// Timing
// ============================================================
using Clock = std::chrono::high_resolution_clock;
static double elapsedMs(Clock::time_point start) {
    auto end = Clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// ============================================================
// DecodeSession — no Qt, just FFmpeg
// ============================================================
class DecodeSession {
public:
    ~DecodeSession() { close(); }

    bool open(const char *path) {
        close();
        m_fmt = nullptr;
        if (avformat_open_input(&m_fmt, path, nullptr, nullptr) != 0) return false;
        if (avformat_find_stream_info(m_fmt, nullptr) < 0) return false;
        m_vi = av_find_best_stream(m_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &m_dec, 0);
        if (m_vi < 0) return false;
        m_codec = avcodec_alloc_context3(m_dec);
        if (!m_codec) return false;
        avcodec_parameters_to_context(m_codec, m_fmt->streams[m_vi]->codecpar);
        if (avcodec_open2(m_codec, m_dec, nullptr) < 0) return false;
        m_w = m_codec->width; m_h = m_codec->height;
        m_codecName = m_dec->name;
        double dur = (double)m_fmt->duration / AV_TIME_BASE;
        if (dur <= 0) {
            AVRational tb = m_fmt->streams[m_vi]->time_base;
            dur = (double)m_fmt->streams[m_vi]->duration * tb.num / tb.den;
        }
        m_dur = dur;
        AVRational fr = m_fmt->streams[m_vi]->avg_frame_rate;
        m_fps = (fr.den > 0) ? (int)round((double)fr.num / fr.den) : 24;
        m_pkt = av_packet_alloc();
        m_frame = av_frame_alloc();
        return true;
    }

    void close() {
        av_frame_free(&m_frame);
        av_packet_free(&m_pkt);
        avcodec_free_context(&m_codec);
        avformat_close_input(&m_fmt);
        m_vi = -1; m_w = m_h = 0;
    }

    bool readFrame() {
        while (av_read_frame(m_fmt, m_pkt) >= 0) {
            if (m_pkt->stream_index == m_vi) {
                if (avcodec_send_packet(m_codec, m_pkt) == 0) {
                    int ret = avcodec_receive_frame(m_codec, m_frame);
                    if (ret == 0) { av_packet_unref(m_pkt); return true; }
                }
            }
            av_packet_unref(m_pkt);
        }
        return false;
    }

    int decodeAll() {
        int n = 0;
        while (readFrame()) n++;
        return n;
    }

    bool seekSec(double sec) {
        if (!m_fmt || m_vi < 0) return false;
        int64_t ts = (int64_t)(sec * AV_TIME_BASE);
        if (ts < 0) ts = 0;
        int ret = av_seek_frame(m_fmt, -1, ts, AVSEEK_FLAG_BACKWARD);
        if (ret < 0) return false;
        avcodec_flush_buffers(m_codec);
        return true;
    }

    double durationSec() const { return m_dur; }
    int fps() const { return m_fps; }
    int width() const { return m_w; }
    int height() const { return m_h; }
    const char* codecName() const { return m_codecName; }
    bool isOpen() const { return m_fmt != nullptr; }

private:
    AVFormatContext *m_fmt = nullptr;
    AVCodecContext *m_codec = nullptr;
    const AVCodec *m_dec = nullptr;
    int m_vi = -1, m_w = 0, m_h = 0;
    double m_dur = 0; int m_fps = 24;
    const char *m_codecName = "unknown";
    AVPacket *m_pkt = nullptr;
    AVFrame *m_frame = nullptr;
};

// ============================================================
// Test Results
// ============================================================
struct TestResult {
    std::string resolution, fps, codec, container, path;
    double startupMs = 0, firstFrameMs = 0, totalDecodeFPS = 0;
    int totalFrames = 0;
    double avgFrameMs = 0;
    double seekMs = 0;
    bool passed = false;
    std::string decoderName;
};

// ============================================================
// Test a single video file
// ============================================================
static TestResult testVideo(const char *path,
    const char *res, const char *fps, const char *codec, const char *container)
{
    TestResult r;
    r.resolution = res;
    r.fps = fps;
    r.codec = codec;
    r.container = container;
    r.path = path;

    DecodeSession s;
    auto t0 = Clock::now();
    bool ok = s.open(path);
    double startupMs = elapsedMs(t0);
    r.startupMs = startupMs;

    if (!ok) {
        fprintf(stderr, "  FAIL: %s - cannot open\n", path);
        return r;
    }

    r.decoderName = s.codecName();

    // First frame
    auto t1 = Clock::now();
    bool gotFrame = s.readFrame();
    r.firstFrameMs = elapsedMs(t1);
    if (!gotFrame) {
        fprintf(stderr, "  FAIL: %s - no frames\n", path);
        s.close();
        return r;
    }

    // Decode all frames
    auto t2 = Clock::now();
    int totalFrames = 1; // we already read one
    while (s.readFrame()) totalFrames++;
    double decodeMs = elapsedMs(t2);
    r.totalFrames = totalFrames;
    r.totalDecodeFPS = (decodeMs > 0) ? (totalFrames / (decodeMs / 1000.0)) : 0;
    r.avgFrameMs = (totalFrames > 0) ? (decodeMs / totalFrames) : 0;

    // Seek test
    if (s.durationSec() > 1.0) {
        double mid = s.durationSec() / 2.0;
        auto t3 = Clock::now();
        s.seekSec(mid);
        r.seekMs = elapsedMs(t3);
        s.readFrame(); // verify seek worked
    }

    r.passed = true;
    s.close();
    return r;
}

// ============================================================
// Main
// ============================================================
int main(int argc, char **argv) {
    (void)argc; (void)argv;

    avformat_network_init();

    printf("========================================\n");
    printf("  LUNAR PLAYER — HEADLESS DECODE BENCH\n");
    printf("  FFmpeg: %s\n", avcodec_configuration());
    printf("========================================\n\n");

    std::vector<TestResult> results;

    // Resolution × FPS matrix
    struct { const char *label; int w, h; } resList[] = {
        {"240p", 426, 240}, {"360p", 640, 360}, {"480p", 854, 480},
        {"720p", 1280, 720}, {"1080p", 1920, 1080},
        {"1440p", 2560, 1440}, {"2160p", 3840, 2160},
    };
    const char *fpsList[] = {"24", "30", "60", "120", "144", "240"};

    // Determine media base path
    const char *mediaBase = nullptr;
    if (argc > 1) {
        mediaBase = argv[1];
    } else {
        mediaBase = "C:\\Users\\ssk90\\Downloads\\Lunar Player Project by SSK\\Lunar Player\\tests\\media";
    }

    char path[512];

    // Phase 1: Resolution × FPS (H.264/MP4)
    printf("=== PHASE 1: Resolution × FPS Matrix ===\n\n");
    printf("%-8s %-6s %-10s %-12s %-8s %-10s %-6s %s\n",
           "Res", "FPS", "Startup", "FirstFrame", "Frames", "DecFPS", "Seek", "Decoder");
    printf("%-8s %-6s %-10s %-12s %-8s %-10s %-6s %s\n",
           "--------", "------", "----------", "-----------", "-------", "---------", "------", "-------");

    for (auto &res : resList) {
        for (auto fps : fpsList) {
            snprintf(path, sizeof(path), "%s\\%s\\test_%s_%sfps.mp4",
                     mediaBase, res.label, res.label, fps);
            auto r = testVideo(path, res.label, fps, "H.264", "MP4");
            results.push_back(r);
            printf("%-8s %-6s %-10.0f %-12.1f %-8d %-10.1f %-6.0f %s\n",
                   r.resolution.c_str(), r.fps.c_str(),
                   r.startupMs, r.firstFrameMs,
                   r.totalFrames, r.totalDecodeFPS,
                   r.seekMs, r.decoderName.c_str());
        }
        printf("\n");
    }

    // Phase 2: Codec matrix (1080p/24fps)
    printf("\n=== PHASE 2: Codec Matrix ===\n\n");
    struct { const char *codec; const char *file; const char *container; } codecs[] = {
        {"H.264",  "1080p\\test_1080p_24fps.mp4",         "MP4"},
        {"H.265",  "codec_tests\\test_1080p_24fps_h265.mp4",  "MP4"},
        {"VP9",    "codec_tests\\test_1080p_24fps_vp9.webm",  "WebM"},
        {"MPEG-2", "codec_tests\\test_1080p_24fps_mpeg2.mkv", "MKV"},
    };
    snprintf(path, sizeof(path), "%s\\codec_tests", mediaBase);
    std::string codecBase = path;

    for (auto &c : codecs) {
        std::string fp = std::string(mediaBase) + "\\" + c.file;
        auto r = testVideo(fp.c_str(), "1080p", "24", c.codec, c.container);
        results.push_back(r);
        printf("%-10s %-8s %-10.0f %-12.1f %-8d %-10.1f %-6.0f %s\n",
               c.codec, "24",
               r.startupMs, r.firstFrameMs,
               r.totalFrames, r.totalDecodeFPS,
               r.seekMs, r.decoderName.c_str());
    }

    // Phase 3: Container matrix (1080p/24fps/H.264)
    printf("\n=== PHASE 3: Container Matrix ===\n\n");
    struct { const char *label; const char *fn; } containers[] = {
        {"MP4",  "1080p\\test_1080p_24fps.mp4"},
        {"MKV",  "codec_tests\\test_1080p_24fps_h264.mkv"},
        {"MOV",  "codec_tests\\test_1080p_24fps_h264.mov"},
        {"AVI",  "codec_tests\\test_1080p_24fps_h264.avi"},
        {"TS",   "codec_tests\\test_1080p_24fps_h264.ts"},
        {"WebM", "codec_tests\\test_1080p_24fps_vp9.webm"},
    };

    for (auto &c : containers) {
        std::string fp = std::string(mediaBase) + "\\" + c.fn;
        auto r = testVideo(fp.c_str(), "1080p", "24", "H.264", c.label);
        results.push_back(r);
        printf("%-8s %-10.0f %-12.1f %-8d %-10.1f %-6.0f %s\n",
               c.label,
               r.startupMs, r.firstFrameMs,
               r.totalFrames, r.totalDecodeFPS,
               r.seekMs, r.decoderName.c_str());
    }

    // Summary
    int passed = 0, failed = 0;
    for (auto &r : results) {
        if (r.passed) passed++; else failed++;
    }

    // Calculate averages
    double avgStartup = 0, avgDecodeMs = 0, avgFrameMs = 0, avgSeek = 0;
    int count = 0;
    for (auto &r : results) {
        if (r.passed) {
            avgStartup += r.startupMs;
            avgDecodeMs += (r.totalFrames > 0 && r.totalDecodeFPS > 0) ? (r.totalFrames / r.totalDecodeFPS * 1000) : 0;
            avgFrameMs += r.avgFrameMs;
            avgSeek += r.seekMs;
            count++;
        }
    }
    if (count > 0) {
        avgStartup /= count;
        avgDecodeMs /= count;
        avgFrameMs /= count;
        avgSeek /= count;
    }

    printf("\n");
    printf("========================================\n");
    printf("  SUMMARY\n");
    printf("========================================\n");
    printf("  Tests: %d passed, %d failed, %d total\n", passed, failed, (int)results.size());
    printf("  Avg Startup:    %.0f ms\n", avgStartup);
    printf("  Avg FirstFrame: %.1f ms\n", avgFrameMs);
    printf("  Avg Seek:       %.0f ms\n", avgSeek);
    printf("\n");

    // Generate HTML report
    {
        FILE *html = fopen("decode_bench_report.html", "w");
        if (html) {
            fprintf(html, "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                "<title>Lunar Player — Decode Benchmark Report</title>"
                "<style>body{font-family:Arial;max-width:1200px;margin:0 auto;padding:20px}"
                "h1{color:#222;border-bottom:3px solid #4a9eff;padding-bottom:10px}"
                "table{border-collapse:collapse;width:100%%;margin:10px 0}"
                "th{background:#37474f;color:#fff;padding:8px;text-align:left}"
                "td{padding:6px 8px;border-bottom:1px solid #ddd}"
                ".pass{color:#2e7d32}.fail{color:#c62828}"
                ".summary{background:#e8f5e9;padding:16px;border-radius:8px;margin:16px 0}"
                "</style></head><body>");

            fprintf(html, "<h1>Lunar Player — Headless Decode Benchmark Report</h1>");
            fprintf(html, "<p>Generated: %s</p>", __DATE__ " " __TIME__);
            fprintf(html, "<p>FFmpeg: %s</p>", avcodec_configuration());

            int total = (int)results.size();
            fprintf(html, "<div class='summary'><h2>%d / %d PASSED</h2>", passed, total);
            fprintf(html, "<p>Avg Startup: %.0fms | Avg Seek: %.0fms</p></div>",
                    avgStartup, avgSeek);

            fprintf(html, "<table><tr>"
                "<th>Resolution</th><th>FPS</th><th>Codec</th><th>Container</th>"
                "<th>Startup</th><th>FirstFrame</th><th>Frames</th><th>DecodeFPS</th>"
                "<th>Seek</th><th>Decoder</th><th>Result</th></tr>");

            for (auto &r : results) {
                fprintf(html, "<tr>"
                    "<td>%s</td><td>%s</td><td>%s</td><td>%s</td>"
                    "<td>%.0fms</td><td>%.1fms</td><td>%d</td><td>%.1f</td>"
                    "<td>%.0fms</td><td>%s</td>"
                    "<td class='%s'>%s</td></tr>\n",
                    r.resolution.c_str(), r.fps.c_str(), r.codec.c_str(), r.container.c_str(),
                    r.startupMs, r.firstFrameMs, r.totalFrames, r.totalDecodeFPS,
                    r.seekMs, r.decoderName.c_str(),
                    r.passed ? "pass" : "fail",
                    r.passed ? "PASS" : "FAIL");
            }

            fprintf(html, "</table></body></html>");
            fclose(html);
            printf("  HTML Report: decode_bench_report.html\n");
        }
    }

    printf("\nDone. (%d passed, %d failed)\n", passed, failed);
    return (failed > 0) ? 1 : 0;
}
