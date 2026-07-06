// Phase 6C: Professional Image Sequence & EXR Support
// Exit: 0 = PASS

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <cstdio>
#include "ui/MainWindow.h"
#include "ui/SequenceFrameCache.h"

#define TEST(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fflush(stdout); return 1; } \
    printf("PASS: %s\n", msg); fflush(stdout); \
} while(0)

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Lunar Player Phase 6C Verification");

    QString testDir = QDir::cleanPath(
        QApplication::applicationDirPath() + "/../../tests/media");
    QString testVideo = testDir + "/test_bars_24fps_2s.mp4";
    QString exrFile = testDir + "/exr_seq/frame_0001.exr";
    QString exrPattern = testDir + "/exr_seq/frame_%04d.exr";

    QString srcTestDir = QDir::cleanPath(
        QString::fromLocal8Bit(TEST_SOURCE_DIR) + "/tests/media");
    if (!QFile::exists(testVideo))
        testVideo = srcTestDir + "/test_bars_24fps_2s.mp4";
    if (!QFile::exists(exrFile))
        exrFile = srcTestDir + "/exr_seq/frame_0001.exr";
    if (!QFile::exists(exrPattern.arg(1)))
        exrPattern = srcTestDir + "/exr_seq/frame_%04d.exr";

    printf("P6C: exr_file=%s\n", qPrintable(QDir::toNativeSeparators(exrFile)));
    printf("P6C: exr_pattern=%s\n", qPrintable(QDir::toNativeSeparators(exrPattern)));
    fflush(stdout);

    // ============================================================
    // 1a. EXR single file decode via MediaSession::open
    // ============================================================
    printf("--- 1. EXR Decode (single file) ---\n"); fflush(stdout);

    {
        MediaSession session;
        TEST(QFile::exists(exrFile), "EXR test file exists");
        TEST(session.open(exrFile), "open single EXR file via MediaSession");
        TEST(session.isOpen(), "session is open after EXR load");

        printf("  EXR: %dx%d, codec=%s, pixfmt=%s\n",
               session.width(), session.height(),
               qPrintable(session.mediaInfo().codecName),
               qPrintable(session.mediaInfo().pixelFormat));
        fflush(stdout);

        TEST(session.width() > 0, "EXR width > 0");
        TEST(session.height() > 0, "EXR height > 0");
        TEST(!session.mediaInfo().codecName.isEmpty(), "EXR codec name populated");
        TEST(!session.mediaInfo().decoderPath.isEmpty(), "decoder path populated");

        TEST(session.readFrame(), "read EXR frame");
        QImage frame = session.currentFrame();
        TEST(!frame.isNull(), "EXR decoded frame is not null");
        TEST(frame.width() == session.width(), "decoded width matches session width");
        TEST(frame.height() == session.height(), "decoded height matches session height");

        QRgb pixel = frame.pixel(10, 10);
        printf("  EXR pixel(10,10) R=%d G=%d B=%d\n",
               qRed(pixel), qGreen(pixel), qBlue(pixel));
        fflush(stdout);

        session.close();
        TEST(!session.isOpen(), "session closed after EXR");
        printf("  EXR single decode OK\n"); fflush(stdout);
    }

    // ============================================================
    // 2. EXR image sequence via openImageSequence
    // ============================================================
    printf("--- 2. EXR Image Sequence ---\n"); fflush(stdout);

    {
        MediaSession session;
        TEST(session.openImageSequence(exrPattern, 1, 10, 24),
             "open EXR image sequence via image2 demuxer");
        TEST(session.isImageSequence(), "session reports isImageSequence");
        TEST(session.fps() == 24, "sequence fps = 24");
        TEST(session.frameCount() == 10, "sequence frame count = 10");
        TEST(session.durationSec() > 0.0, "sequence duration > 0");

        int frameCount = 0;
        while (session.readFrame()) {
            QImage f = session.currentFrame();
            TEST(!f.isNull(), "sequence frame decoded");
            printf("  frame %d: %dx%d\n", frameCount+1, f.width(), f.height());
            fflush(stdout);
            frameCount++;
        }
        printf("  decoded %d/%d EXR sequence frames\n", frameCount, 10);
        fflush(stdout);
        TEST(frameCount >= 10, "all 10 EXR sequence frames decoded");

        session.seekSec(0);
        TEST(session.readFrame(), "seek+read first frame after seek");
        QImage reFrame = session.currentFrame();
        TEST(!reFrame.isNull(), "re-read frame is valid after seek");

        session.close();
        printf("  EXR image sequence OK\n"); fflush(stdout);
    }

    // ============================================================
    // 3. Video playback regression
    // ============================================================
    printf("--- 3. Video Playback Regression ---\n"); fflush(stdout);

    {
        MediaSession session;
        TEST(session.open(testVideo), "open video file");
        TEST(session.fps() == 24, "video fps = 24");
        TEST(session.width() == 320, "video width = 320");
        TEST(session.height() == 240, "video height = 240");

        int frames = 0;
        while (session.readFrame() && frames < 50) frames++;
        TEST(frames >= 10, "decoded at least 10 video frames");

        session.seekSec(0);
        TEST(session.readFrame(), "seek back and read video frame after seek");
        QImage vf = session.currentFrame();
        TEST(!vf.isNull(), "video frame decoded after seek");

        session.close();
        printf("  Video playback OK\n"); fflush(stdout);
    }

    // ============================================================
    // 4. SequenceFrameCache unit tests (last — cleanup race on exit)
    // ============================================================
    printf("--- 4. SequenceFrameCache ---\n"); fflush(stdout);

    {
        SequenceFrameCache cache;
        printf("  cache created\n"); fflush(stdout);
        TEST(!cache.isConfigured(), "cache starts unconfigured");

        cache.configure("/some/pattern/%04d.exr", 1, 50, 1920, 1080, AV_PIX_FMT_NONE);
        TEST(cache.isConfigured(), "cache configured");
        TEST(cache.totalFrames() == 50, "totalFrames = 50");
        TEST(cache.startFrame() == 1, "startFrame = 1");
        TEST(cache.cachedFrames() == 0, "0 frames cached initially");
        TEST(cache.cacheMemoryBytes() == 0, "0 bytes cached initially");

        // Test that hasFrame/cachedFrames work correctly
        printf("  testing frame presence...\n"); fflush(stdout);
        TEST(!cache.hasFrame(0), "hasFrame(0) returns false initially");
        TEST(!cache.hasFrame(5), "hasFrame(5) returns false initially");

        // Prefetch with non-existent pattern (should not crash)
        printf("  testing prefetch (non-existent files)...\n"); fflush(stdout);
        cache.prefetch(5, 2);
        QThread::msleep(100);

        TEST(cache.cachedFrames() == 0, "0 frames cached after prefetch of nonexistent files");
        TEST(cache.cacheMemoryBytes() == 0, "0 bytes after prefetch failure");

        cache.clear();
        TEST(!cache.isConfigured(), "cache cleared");

        printf("  destroying cache (scope exit)...\n"); fflush(stdout);
    }
    printf("  cache destroyed successfully\n"); fflush(stdout);

    // ============================================================
    // All passed
    // ============================================================
    printf("P6C: ALL PASS\n");
    return 0;
}
