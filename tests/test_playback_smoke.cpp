// Phase 2B Playback Smoke Test
// Validates the full FFmpeg -> MediaSession -> VideoWidget -> GLVideoRenderer -> Screen pipeline.
// Requires native display (fails on QT_QPA_PLATFORM=offscreen).
// Run: build\LunarPlayerPlaybackSmoke.exe
// Exit: 0 = PASS

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <cstdio>
#include "ui/MainWindow.h"
#include "ui/VideoWidget.h"

static double avgPixel(const QImage &img)
{
    if (img.isNull()) return -1;
    double sum = 0; int count = 0;
    for (int y = 0; y < qMin(img.height(), 32); y++) {
        const uchar *line = img.constScanLine(y);
        for (int x = 0; x < qMin(img.width(), 32) * 3; x++) { sum += line[x]; count++; }
    }
    return (count > 0) ? (sum / count) : -1;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Lunar Player Playback Smoke");

    QString testVideo = QDir::cleanPath(
        QApplication::applicationDirPath() + "/../../tests/media/test_bars_24fps_2s.mp4");
    if (!QFile::exists(testVideo))
        testVideo = QDir::cleanPath(
            QString::fromLocal8Bit(TEST_SOURCE_DIR) + "/tests/media/test_bars_24fps_2s.mp4");

    printf("SMOKE: video=%s\n", qPrintable(QDir::toNativeSeparators(testVideo)));
    fflush(stdout);

    // 1. Create window with OpenGL renderer
    MainWindow window;
    window.resize(800, 500);
    window.show();
    for (int i = 0; i < 20; i++) QApplication::processEvents();

    printf("SMOKE: window title=%s\n", qPrintable(window.windowTitle()));
    fflush(stdout);

    // 2. Open video via MediaSession directly
    if (!window.session()->open(testVideo)) {
        printf("SMOKE FAIL: video not opened\n"); return 1;
    }
    window.session()->readFrame();
    window.videoWidget()->setFrame(window.session()->currentFrame());

    printf("SMOKE: open %dx%d %.2fs %dfps\n",
           window.session()->width(), window.session()->height(),
           window.session()->durationSec(), window.session()->fps());
    fflush(stdout);

    if (window.session()->width() != 320 || window.session()->height() != 240) {
        printf("SMOKE FAIL: wrong dimensions\n"); return 1;
    }

    // 3. Capture first rendered frame - verify non-null with valid content
    for (int i = 0; i < 5; i++) QApplication::processEvents();
    QImage snap0 = window.videoWidget()->grabFramebuffer();
    double avg0 = avgPixel(snap0);
    printf("SMOKE: frame0 avg=%.2f size=%dx%d\n", avg0, snap0.width(), snap0.height());
    fflush(stdout);
    if (snap0.isNull() || snap0.width() < 100) {
        printf("SMOKE FAIL: framebuffer invalid\n"); return 1;
    }
    if (avg0 < 0) {
        printf("SMOKE FAIL: frame0 empty\n"); return 1;
    }
    printf("SMOKE: render output: PASS\n");
    fflush(stdout);

    // 4. Play all frames through decoder - verify decoder advances
    int frameCount = 1;
    while (window.session()->readFrame()) {
        window.videoWidget()->setFrame(window.session()->currentFrame());
        QApplication::processEvents();
        frameCount++;
    }
    QApplication::processEvents();
    printf("SMOKE: decoded %d frames\n", frameCount);
    fflush(stdout);
    if (frameCount < 10) {
        printf("SMOKE FAIL: too few frames decoded\n"); return 1;
    }
    printf("SMOKE: decoder advancement: PASS\n");
    fflush(stdout);

    // 5. Re-read first frame - verify decoder repositions correctly
    window.session()->seekSec(0);
    window.session()->readFrame();
    window.videoWidget()->setFrame(window.session()->currentFrame());
    QApplication::processEvents();
    QImage snapSeek0 = window.videoWidget()->grabFramebuffer();
    double avgSeek0 = avgPixel(snapSeek0);
    printf("SMOKE: rewind avg=%.2f (close to %.2f)\n", avgSeek0, avg0);
    fflush(stdout);
    if (qAbs(avgSeek0 - avg0) > 10.0) {
        printf("SMOKE FAIL: rewind produced different frame\n"); return 1;
    }
    printf("SMOKE: seek+rewind: PASS\n");
    fflush(stdout);

    // 6. Resize - verify no crash
    window.resize(1024, 768);
    QApplication::processEvents();
    QImage snapResize = window.videoWidget()->grabFramebuffer();
    if (snapResize.isNull()) {
        printf("SMOKE FAIL: framebuffer after resize invalid\n"); return 1;
    }
    printf("SMOKE: resize: PASS (%dx%d)\n", snapResize.width(), snapResize.height());
    fflush(stdout);

    // 7. Close session
    window.session()->close();
    window.videoWidget()->clearFrame();
    QApplication::processEvents();
    printf("SMOKE: close: PASS\n");
    fflush(stdout);

    // 8. Reopen + close cycle
    if (!window.session()->open(testVideo)) {
        printf("SMOKE FAIL: reopen failed\n"); return 1;
    }
    QApplication::processEvents();
    window.session()->close();
    QApplication::processEvents();
    printf("SMOKE: reopen+close: PASS\n");
    fflush(stdout);

    // 9. Final open + render
    if (!window.session()->open(testVideo)) {
        printf("SMOKE FAIL: final open failed\n"); return 1;
    }
    window.session()->readFrame();
    window.videoWidget()->setFrame(window.session()->currentFrame());
    QApplication::processEvents();
    QImage snapFinal = window.videoWidget()->grabFramebuffer();
    if (snapFinal.isNull()) {
        printf("SMOKE FAIL: final render invalid\n"); return 1;
    }
    printf("SMOKE: final open: PASS\n");
    fflush(stdout);

    printf("SMOKE: ALL PASS\n");
    return 0;
}
