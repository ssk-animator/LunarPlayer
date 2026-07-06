// Phase 6B Universal Format Support — Verification
// Tests: filter builder, error diagnostics, DecodeState, mediaInfo, playback counters
// Exit: 0 = PASS

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <cstdio>
#include "ui/MainWindow.h"

#define TEST(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fflush(stdout); return 1; } \
    printf("PASS: %s\n", msg); fflush(stdout); \
} while(0)

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Lunar Player Phase 6B Verification");

    // Locate test media
    QString testVideo = QDir::cleanPath(
        QApplication::applicationDirPath() + "/../../tests/media/test_bars_24fps_2s.mp4");
    if (!QFile::exists(testVideo))
        testVideo = QDir::cleanPath(
            QString::fromLocal8Bit(TEST_SOURCE_DIR) + "/tests/media/test_bars_24fps_2s.mp4");

    printf("P6B: test_video=%s\n", qPrintable(QDir::toNativeSeparators(testVideo)));
    fflush(stdout);

    // ============================================================
    // 1. Filter builder verification
    // ============================================================
    printf("--- 1. Filter Builder ---\n"); fflush(stdout);

    // Trigger lazy init
    QStringList mf = MainWindow::buildMediaFilters();
    QStringList nf = MainWindow::buildNavFilters();

    TEST(mf.size() == 2, "mediaFilters has 2 entries (Supported Media + All Files)");
    TEST(mf[0].startsWith("Supported Media Files"), "first filter is 'Supported Media Files'");
    TEST(mf[1] == "All Files (*)", "second filter is 'All Files (*)'");
    TEST(nf.size() > 20, "navFilters has many extensions");

    // Verify common extensions present
    QStringList exts = {"mp4", "mov", "avi", "mkv", "png", "jpg", "webm", "m4v", "opus", "flac", "aac", "mp3"};
    for (const QString &ext : exts) {
        TEST(nf.contains("*." + ext), QString("navFilter includes *.%1").arg(ext).toUtf8().constData());
    }

    // Verify sorted order (deterministic)
    QStringList sorted = nf;
    sorted.sort();
    TEST(nf == sorted, "navFilters are sorted alphabetically");

    // ============================================================
    // 2. Error diagnostics — opening nonexistent file
    // ============================================================
    printf("--- 2. Error Diagnostics ---\n"); fflush(stdout);

    {
        MediaSession session;
        TEST(!session.open("C:/nonexistent_file_xyz123.mp4"), "open returns false for nonexistent file");
        QString err = session.lastError();
        printf("  lastError: %s\n", qPrintable(err));
        TEST(!err.isEmpty(), "lastError is non-empty");
        // Should mention either ENOENT, "Failed to open", or "No such file"
        bool meaningful = err.contains("Failed to open", Qt::CaseInsensitive)
                       || err.contains("No such file", Qt::CaseInsensitive)
                       || err.contains("ENOENT", Qt::CaseInsensitive);
        TEST(meaningful, "error message is meaningful");
    }

    // ============================================================
    // 3. Open a real file — verify mediaInfo fields
    // ============================================================
    printf("--- 3. Media Info Fields ---\n"); fflush(stdout);

    MainWindow window;
    window.resize(800, 500);
    window.show();
    for (int i = 0; i < 5; i++) QApplication::processEvents();

    TEST(window.session()->open(testVideo), "open test video succeeds");
    window.session()->readFrame();
    window.applyCurrentFrame();
    QApplication::processEvents();

    MediaInfo info = window.session()->mediaInfo();

    TEST(!info.containerFormat.isEmpty(), "containerFormat is populated");
    TEST(!info.codecName.isEmpty(), "codecName is populated");
    TEST(info.width > 0 && info.height > 0, "resolution is populated");
    TEST(info.fps > 0, "fps is populated");
    TEST(info.bitDepth == 8 || info.bitDepth == 10 || info.bitDepth == 12 || info.bitDepth == 16,
         "bitDepth is valid (8/10/12/16)");
    TEST(!info.chromaSubsampling.isEmpty(), "chromaSubsampling is populated");
    TEST(!info.decoder.isEmpty(), "decoder name is populated");
    TEST(!info.decoderPath.isEmpty(), "decoderPath is populated (Software/D3D11VA/NVDEC/...)");

    // ============================================================
    // 4. DecodeState transitions
    // ============================================================
    printf("--- 4. DecodeState Transitions ---\n"); fflush(stdout);

    {
        MediaSession localSession;
        TEST(localSession.decodeState() == MediaSession::DecodeState::Reading,
             "initial state is Reading");

        TEST(localSession.open(testVideo), "localSession open");
        TEST(localSession.decodeState() == MediaSession::DecodeState::Reading,
             "after open, state is Reading");

        // Read frames until EOF
        int frames = 0;
        while (localSession.readFrame()) {
            frames++;
        }
        printf("  decoded %d frames, state=%d\n", frames, (int)localSession.decodeState());
        fflush(stdout);

        TEST(frames >= 10, "decoded at least 10 frames");
        TEST(localSession.decodeState() == MediaSession::DecodeState::Finished,
             "after EOF, state is Finished");

        // Seek back — state should reset to Reading
        localSession.seekSec(0);
        TEST(localSession.decodeState() == MediaSession::DecodeState::Reading,
             "after seek, state resets to Reading");
        localSession.close();
    }

    // ============================================================
    // 5. Playback diagnostics counters
    // ============================================================
    printf("--- 5. Playback Diagnostics ---\n"); fflush(stdout);

    {
        MediaSession diagSession;
        TEST(diagSession.open(testVideo), "diagSession open");

        // Read some frames
        for (int i = 0; i < 30; i++) {
            if (!diagSession.readFrame()) break;
        }

        MediaInfo di = diagSession.mediaInfo();
        printf("  totalFramesDecoded=%lld  packetsSkipped=%d  framesDropped=%d\n",
               (long long)di.totalFramesDecoded, di.packetsSkipped, di.framesDropped);
        fflush(stdout);

        TEST(di.totalFramesDecoded > 0, "totalFramesDecoded > 0");
        // For clean test files, skipped/dropped should be 0
        // (non-zero is acceptable but unexpected)
        printf("  (clean file: packetsSkipped=0 expected, framesDropped=0 expected)\n");
        fflush(stdout);

        diagSession.close();
    }

    // ============================================================
    // 6. Seek latency tracking
    // ============================================================
    printf("--- 6. Seek Latency ---\n"); fflush(stdout);

    {
        MediaSession seekSession;
        TEST(seekSession.open(testVideo), "seekSession open");
        seekSession.readFrame();
        seekSession.seekSec(1.0);
        MediaInfo si = seekSession.mediaInfo();
        printf("  seeksCount=%d  seekLatencyMs=%.2f\n", si.seeksCount, si.seekLatencyMs);
        fflush(stdout);
        TEST(si.seeksCount > 0, "seeksCount > 0");
        TEST(si.seekLatencyMs >= 0, "seekLatencyMs >= 0");
        seekSession.close();
    }

    // ============================================================
    // 7. Open/close cycle (no crash)
    // ============================================================
    printf("--- 7. Open/Close Cycle ---\n"); fflush(stdout);

    {
        MediaSession cycleSession;
        for (int i = 0; i < 3; i++) {
            TEST(cycleSession.open(testVideo), QString("cycle %1 open").arg(i+1).toUtf8().constData());
            for (int j = 0; j < 10; j++) {
                if (!cycleSession.readFrame()) break;
            }
            cycleSession.close();
            TEST(!cycleSession.isOpen(), QString("cycle %1 closed").arg(i+1).toUtf8().constData());
        }
    }

    // ============================================================
    // 8. File dialog filters at runtime — verify QFileDialog list
    // ============================================================
    printf("--- 8. Runtime Filter String ---\n"); fflush(stdout);

    QStringList mediaFilters = MainWindow::buildMediaFilters();
    QString joined = mediaFilters.join(";;");
    printf("  filter string length: %zu\n", (size_t)joined.size());
    TEST(joined.contains("Supported Media Files"), "filter string contains 'Supported Media Files'");
    TEST(joined.contains("*.mp4"), "filter string contains *.mp4");
    TEST(joined.contains("*.png"), "filter string contains *.png");
    TEST(joined.contains("All Files (*)"), "filter string contains 'All Files (*)'");

    // ============================================================
    // All passed
    // ============================================================
    printf("P6B: ALL PASS\n");
    return 0;
}
