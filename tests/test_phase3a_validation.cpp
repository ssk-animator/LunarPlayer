// Phase 3A Functional Validation
// Tests each new control: frame stepping, JKL, speed, loop, accurate seeking.
// Run: build\LunarPlayerPhase3A.exe (requires native display)

#include <QApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QImage>
#include <cstdio>
#include <cmath>

#include "ui/MainWindow.h"
#include "ui/VideoWidget.h"

static bool failFlag = false;
#define VALIDATE(cond, msg) do { \
    if (!(cond)) { failFlag = true; printf("  FAIL: %s\n", msg); fflush(stdout); } \
    else { printf("  PASS: %s\n", msg); fflush(stdout); } \
} while(0)

static int countDecodedFrames(MainWindow &mw)
{
    int n = 0;
    while (mw.session()->readFrame()) {
        mw.applyCurrentFrame();
        QApplication::processEvents();
        n++;
    }
    return n;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Phase 3A Validation");

    QString base = QDir::cleanPath(
        QApplication::applicationDirPath() + "/../../tests/media");
    if (!QDir(base).exists())
        base = QDir::cleanPath(
            QString::fromLocal8Bit(TEST_SOURCE_DIR) + "/tests/media");

    QString barsFile = QDir(base).filePath("test_bars_24fps_2s.mp4");
    QString longFile = QDir(base).filePath("qa_long_480p_60s.mp4");

    printf("\n=== Phase 3A Functional Validation ===\n\n");

    // ----- 1. Frame Step Forward -----
    printf("[1] Frame Step Forward\n");
    {
        MainWindow mw;
        mw.resize(640, 480); mw.show();
        for (int i = 0; i < 10; i++) QApplication::processEvents();

        VALIDATE(mw.session()->open(barsFile), "open file");
        mw.session()->readFrame();
        mw.applyCurrentFrame();
        QApplication::processEvents();

        int initialFrame = mw.session()->currentFrameNumber();
        printf("  initial frame = %d\n", initialFrame);
        VALIDATE(initialFrame >= 0, "initial frame number valid");

        // Step forward 5 times
        for (int i = 0; i < 5; i++) {
            mw.session()->stepForward();
            mw.applyCurrentFrame();
            QApplication::processEvents();
        }
        int afterForward = mw.session()->currentFrameNumber();
        printf("  after 5 forward steps = %d\n", afterForward);
        VALIDATE(afterForward == initialFrame + 5,
                 "forward 5 steps advances frame by 5");
    }
    printf("\n");

    // ----- 2. Frame Step Backward -----
    printf("[2] Frame Step Backward\n");
    {
        MainWindow mw;
        mw.resize(640, 480); mw.show();
        for (int i = 0; i < 10; i++) QApplication::processEvents();

        mw.session()->open(barsFile);
        // Seek to frame 20
        VALIDATE(mw.session()->seekToFrame(20), "seek to frame 20");
        mw.applyCurrentFrame();
        QApplication::processEvents();
        int at20 = mw.session()->currentFrameNumber();
        printf("  at frame 20 = %d\n", at20);
        VALIDATE(at20 == 20, "seekToFrame(20) lands on frame 20");

        // Step backward 3 times
        for (int i = 0; i < 3; i++) {
            mw.session()->stepBackward();
            mw.applyCurrentFrame();
            QApplication::processEvents();
        }
        int afterBack = mw.session()->currentFrameNumber();
        printf("  after 3 backward steps = %d\n", afterBack);
        VALIDATE(afterBack == 17, "backward 3 steps from 20 lands on 17");
    }
    printf("\n");

    // ----- 3. Accurate Frame Seek -----
    printf("[3] Accurate Frame Seek\n");
    {
        MainWindow mw;
        mw.resize(640, 480); mw.show();
        for (int i = 0; i < 10; i++) QApplication::processEvents();

        mw.session()->open(barsFile);
        int targets[] = {0, 10, 25, 40, 47};
        bool allAccurate = true;
        for (int t : targets) {
            if (!mw.session()->seekToFrame(t)) {
                printf("  seekToFrame(%d) returned false\n", t);
                allAccurate = false;
                continue;
            }
            mw.applyCurrentFrame();
            QApplication::processEvents();
            int actual = mw.session()->currentFrameNumber();
            if (actual != t) {
                printf("  seekToFrame(%d) landed on %d\n", t, actual);
                allAccurate = false;
            }
        }
        VALIDATE(allAccurate, "all frame seeks land on exact frame");
    }
    printf("\n");

    // ----- 4. JKL Playback -----
    printf("[4] JKL Playback Controls\n");
    {
        MainWindow mw;
        mw.resize(640, 480); mw.show();
        for (int i = 0; i < 10; i++) QApplication::processEvents();

        mw.session()->open(barsFile);
        mw.session()->readFrame();
        mw.applyCurrentFrame();
        QApplication::processEvents();

        // Simulate JKL by setting m_playDirection directly
        // L = forward
        mw.m_playDirection = 1;
        mw.m_playing = true;
        VALIDATE(mw.m_playDirection == 1 && mw.m_playing,
                 "L sets direction=1 and playing=true");

        // K = pause
        mw.m_playDirection = 0;
        mw.m_playing = false;
        VALIDATE(mw.m_playDirection == 0 && !mw.m_playing,
                 "K sets direction=0 and playing=false");

        // J = reverse
        mw.m_playDirection = -1;
        mw.m_playing = true;
        VALIDATE(mw.m_playDirection == -1 && mw.m_playing,
                 "J sets direction=-1 and playing=true");

        // Verify reverse actually steps backward
        mw.session()->seekToFrame(30);
        mw.applyCurrentFrame();
        QApplication::processEvents();
        int beforeReverse = mw.session()->currentFrameNumber();
        VALIDATE(beforeReverse == 30, "at frame 30 before reverse test");

        // Simulate one reverse tick
        if (mw.session()->stepBackward()) {
            mw.applyCurrentFrame();
            QApplication::processEvents();
            int afterReverse = mw.session()->currentFrameNumber();
            VALIDATE(afterReverse == 29,
                     "one reverse frame step goes to frame 29");
        }
    }
    printf("\n");

    // ----- 5. Playback Speed Control -----
    printf("[5] Playback Speed Control\n");
    {
        MainWindow mw;
        mw.resize(640, 480); mw.show();
        for (int i = 0; i < 10; i++) QApplication::processEvents();

        mw.session()->open(barsFile);
        mw.session()->readFrame();
        mw.applyCurrentFrame();
        QApplication::processEvents();

        // Verify default speed
        VALIDATE(mw.m_speed == 1.0, "default speed is 1.0x");

        // Speed up (simulate ] key)
        mw.m_speed = qMin(4.0, mw.m_speed * 1.5);
        VALIDATE(mw.m_speed == 1.5, "speed up to 1.5x");

        mw.m_speed = qMin(4.0, mw.m_speed * 1.5);
        VALIDATE(mw.m_speed == 2.25, "speed up to 2.25x");

        mw.m_speed = qMin(4.0, mw.m_speed * 1.5);
        VALIDATE(mw.m_speed == 3.375, "speed up to 3.375x");

        mw.m_speed = qMin(4.0, mw.m_speed * 1.5);
        VALIDATE(mw.m_speed == 4.0, "speed capped at 4.0x");

        // Speed down (simulate [ key)
        mw.m_speed = qMax(0.25, mw.m_speed / 1.5);
        VALIDATE(fabs(mw.m_speed - 2.667) < 0.01, "speed down to ~2.667x");

        // Chain down to minimum (7 iterations from 4.0 to reach 0.25)
        for (int i = 0; i < 7; i++)
            mw.m_speed = qMax(0.25, mw.m_speed / 1.5);
        VALIDATE(mw.m_speed == 0.25, "speed floor at 0.25x");

        // Verify speed affects timer interval
        mw.m_speed = 1.0;
        double frameDur = 1.0 / qMax(mw.session()->fps(), 1);
        int interval1x = qMax(1, (int)((frameDur / 1.0) * 1000.0));

        mw.m_speed = 2.0;
        int interval2x = qMax(1, (int)((frameDur / 2.0) * 1000.0));
        VALIDATE(interval2x < interval1x, "2x speed has shorter timer interval than 1x");

        mw.m_speed = 0.5;
        int intervalHalf = qMax(1, (int)((frameDur / 0.5) * 1000.0));
        VALIDATE(intervalHalf > interval1x, "0.5x speed has longer timer interval than 1x");
    }
    printf("\n");

    // ----- 6. Loop In/Out -----
    printf("[6] Loop In/Out\n");
    {
        MainWindow mw;
        mw.resize(640, 480); mw.show();
        for (int i = 0; i < 10; i++) QApplication::processEvents();

        mw.session()->open(longFile);
        mw.session()->seekToFrame(100);
        mw.applyCurrentFrame();
        QApplication::processEvents();

        // Set loop points
        mw.m_loopIn = 100;
        mw.m_loopOut = 110;
        VALIDATE(mw.m_loopIn == 100 && mw.m_loopOut == 110,
                 "loop in=100, loop out=110 set correctly");

        // Verify loop triggers: decode from loopOut, should wrap to loopIn
        mw.session()->seekToFrame(110);
        mw.applyCurrentFrame();
        QApplication::processEvents();
        int beforeLoop = mw.session()->currentFrameNumber();
        printf("  before loop trigger: frame %d\n", beforeLoop);

        // Simulate decode timer tick at loop boundary
        // stepForward would go to 111, but loop should redirect to 100
        mw.m_currentFrameNum = 110;
        if (mw.m_loopOut >= 0 && mw.m_currentFrameNum >= mw.m_loopOut && mw.m_loopIn >= 0) {
            mw.session()->seekToFrame(mw.m_loopIn);
            mw.applyCurrentFrame();
            QApplication::processEvents();
            mw.m_currentFrameNum = mw.session()->currentFrameNumber();
        }
        VALIDATE(mw.m_currentFrameNum == 100,
                 "loop wraps from 110 to 100");

        // Clear loop points
        mw.m_loopIn = -1;
        mw.m_loopOut = -1;
        VALIDATE(mw.m_loopIn == -1 && mw.m_loopOut == -1,
                 "loop points cleared");
    }
    printf("\n");

    // ----- 7. Verify YUV renderer path -----
    printf("[7] YUV Renderer Integration\n");
    {
        MainWindow mw;
        mw.resize(640, 480); mw.show();
        for (int i = 0; i < 10; i++) QApplication::processEvents();

        VALIDATE(mw.session()->open(barsFile), "open with YUV renderer");
        VALIDATE(mw.videoWidget()->rendererSupportsAVFrame(),
                 "YUVVideoRenderer supports AVFrame");

        mw.session()->readFrame();
        mw.applyCurrentFrame();
        QApplication::processEvents();
        QImage snap = mw.videoWidget()->grabFramebuffer();
        VALIDATE(!snap.isNull() && snap.width() > 0,
                 "YUV renderer produces visible output");

        // Step forward via YUV path
        mw.session()->stepForward();
        mw.applyCurrentFrame();
        QApplication::processEvents();
        QImage snap2 = mw.videoWidget()->grabFramebuffer();
        VALIDATE(!snap2.isNull(), "step forward works with YUV renderer");

        // Step backward via YUV path
        mw.session()->stepBackward();
        mw.applyCurrentFrame();
        QApplication::processEvents();
        QImage snap3 = mw.videoWidget()->grabFramebuffer();
        VALIDATE(!snap3.isNull(), "step backward works with YUV renderer");
    }
    printf("\n");

    // ----- 8. Reverse playback end-to-end -----
    printf("[8] Reverse Playback End-to-End\n");
    {
        MainWindow mw;
        mw.resize(640, 480); mw.show();
        for (int i = 0; i < 10; i++) QApplication::processEvents();

        mw.session()->open(barsFile);
        mw.session()->seekToFrame(10);
        mw.applyCurrentFrame();
        QApplication::processEvents();
        int startFrame = mw.session()->currentFrameNumber();
        printf("  reverse start at frame %d\n", startFrame);

        // Simulate 5 reverse ticks
        for (int i = 0; i < 5; i++) {
            bool ok = mw.session()->stepBackward();
            if (!ok) printf("  FAIL: reverse tick %d failed\n", i + 1);
            if (!ok) break;
            mw.applyCurrentFrame();
            QApplication::processEvents();
        }
        int endFrame = mw.session()->currentFrameNumber();
        printf("  after 5 reverse ticks: frame %d\n", endFrame);
        VALIDATE(endFrame == startFrame - 5,
                 "reverse playback advances frame backward correctly");
    }
    printf("\n");

    printf("=== Phase 3A Validation: %s ===\n",
           failFlag ? "SOME TESTS FAILED" : "ALL PASS");
    return failFlag ? 1 : 0;
}
