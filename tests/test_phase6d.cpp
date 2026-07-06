// Phase 6D: Side-by-Side Compare
// Exit: 0 = PASS

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <cstdio>
#include "ui/MainWindow.h"
#include "ui/CompareController.h"

#define TEST(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fflush(stdout); return 1; } \
    printf("PASS: %s\n", msg); fflush(stdout); \
} while(0)

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Lunar Player Phase 6D Verification");

    QString testDir = QDir::cleanPath(
        QApplication::applicationDirPath() + "/../../tests/media");
    QString srcTestDir = QDir::cleanPath(
        QString::fromLocal8Bit(TEST_SOURCE_DIR) + "/tests/media");
    QString video = testDir + "/test_bars_24fps_2s.mp4";
    if (!QFile::exists(video))
        video = srcTestDir + "/test_bars_24fps_2s.mp4";

    printf("P6D: video=%s\n", qPrintable(QDir::toNativeSeparators(video)));
    fflush(stdout);

    // ============================================================
    // 1. CompareController — setSessions, lock modes, sync operations
    // ============================================================
    printf("--- 1. CompareController Core API ---\n"); fflush(stdout);

    {
        MediaSession sa, sb;
        TEST(QFile::exists(video), "test video exists");

        CompareController ctrl;
        TEST(ctrl.lockMode() == CompareLockMode::TimeLock, "default lock mode is TimeLock");
        TEST(ctrl.isSyncLocked() == true, "sync locked by default");

        ctrl.setSessions(&sa, &sb);

        TEST(sa.open(video), "open session A");
        TEST(sb.open(video), "open session B");
        TEST(sa.isOpen(), "session A open");
        TEST(sb.isOpen(), "session B open");

        TEST(sa.readFrame(), "session A first frame");
        TEST(sb.readFrame(), "session B first frame");
        QImage frameA0 = sa.currentFrame();
        QImage frameB0 = sb.currentFrame();
        TEST(!frameA0.isNull(), "session A frame not null");
        TEST(!frameB0.isNull(), "session B frame not null");

        // readBoth
        TEST(ctrl.readBoth(), "readBoth returns true with both open");

        // stepBoth forward
        int startFrameA = sa.currentFrameNumber();
        int startFrameB = sb.currentFrameNumber();
        TEST(ctrl.stepBoth(1), "stepBoth(1) returns true");
        TEST(sa.currentFrameNumber() == startFrameA + 1, "session A stepped forward");
        TEST(sb.currentFrameNumber() == startFrameB + 1, "session B stepped forward");

        // stepBoth backward
        TEST(ctrl.stepBoth(-1), "stepBoth(-1) returns true");
        TEST(sa.currentFrameNumber() == startFrameA, "session A stepped back");
        TEST(sb.currentFrameNumber() == startFrameB, "session B stepped back");

        // seekSync + read both
        ctrl.seekSync(1.0);
        TEST(sa.readFrame() && sb.readFrame(), "read both after seekSync(1.0)");
        // 24fps * 1.0s = frame 24 — allow small tolerance for keyframe alignment
        int fA = sa.currentFrameNumber();
        int fB = sb.currentFrameNumber();
        printf("  seekSync(1.0): A frame=%d B frame=%d\n", fA, fB);
        fflush(stdout);
        TEST(fA >= 12, "session A frame >= 12 after seekSync(1.0)");
        TEST(fB >= 12, "session B frame >= 12 after seekSync(1.0)");

        // seekToFrameSync — seekToFrame already reads the target frame internally
        ctrl.seekToFrameSync(0);
        TEST(sa.currentFrameNumber() == 0, "session A frame 0 after seekToFrameSync");
        TEST(sb.currentFrameNumber() == 0, "session B frame 0 after seekToFrameSync");

        // setLockMode
        ctrl.setLockMode(CompareLockMode::FrameLock);
        TEST(ctrl.lockMode() == CompareLockMode::FrameLock, "lock mode changed to FrameLock");
        ctrl.setLockMode(CompareLockMode::TimeLock);
        TEST(ctrl.lockMode() == CompareLockMode::TimeLock, "lock mode changed back to TimeLock");

        // setSyncLocked toggle
        ctrl.setSyncLocked(false);
        TEST(!ctrl.isSyncLocked(), "syncLocked disabled");
        ctrl.setSyncLocked(true);
        TEST(ctrl.isSyncLocked(), "syncLocked re-enabled");

        sa.close();
        sb.close();
        printf("  CompareController core API OK\n"); fflush(stdout);
    }

    // ============================================================
    // 2. FrameLock vs TimeLock consistency
    // ============================================================
    printf("--- 2. FrameLock / TimeLock consistency ---\n"); fflush(stdout);

    {
        MediaSession sa, sb;
        TEST(sa.open(video), "open A (frame lock test)");
        TEST(sb.open(video), "open B (frame lock test)");

        CompareController ctrl;
        ctrl.setSessions(&sa, &sb);
        TEST(sa.readFrame(), "read A initial");
        TEST(sb.readFrame(), "read B initial");

        // FrameLock — seek to frame 10 (seekToFrame already reads the target)
        ctrl.setLockMode(CompareLockMode::FrameLock);
        ctrl.seekToFrameSync(10);
        TEST(sa.currentFrameNumber() == 10, "FrameLock: session A frame 10");
        TEST(sb.currentFrameNumber() == 10, "FrameLock: session B frame 10");

        // seekSync works regardless of lock mode
        ctrl.seekSync(0.0);
        TEST(sa.readFrame() && sb.readFrame(), "read both after seekSync(0)");
        TEST(sa.currentFrameNumber() == 0, "seekSync: session A frame 0");
        TEST(sb.currentFrameNumber() == 0, "seekSync: session B frame 0");

        // TimeLock — seek to 0.5s
        ctrl.setLockMode(CompareLockMode::TimeLock);
        ctrl.seekSync(0.5);
        TEST(sa.readFrame() && sb.readFrame(), "read both after seekSync(0.5)");
        // 24fps * 0.5s = frame 12 — allow tolerance for keyframe alignment
        int fAt = sa.currentFrameNumber();
        int fBt = sb.currentFrameNumber();
        printf("  TimeLock(0.5): A frame=%d B frame=%d\n", fAt, fBt);
        fflush(stdout);
        TEST(fAt >= 6, "TimeLock: session A frame >= 6 after 0.5s");
        TEST(fBt >= 6, "TimeLock: session B frame >= 6 after 0.5s");

        sa.close();
        sb.close();
        printf("  FrameLock/TimeLock OK\n"); fflush(stdout);
    }

    // ============================================================
    // 3. Independent decode — sessions don't interfere
    // ============================================================
    printf("--- 3. Independent Decode ---\n"); fflush(stdout);

    {
        MediaSession sa, sb;
        TEST(sa.open(video), "open A (independent)");
        TEST(sb.open(video), "open B (independent)");

        for (int i = 0; i < 5; i++) TEST(sa.readFrame(), "A decode frame");
        printf("  decoded 5 A frames, A frame=%d\n", sa.currentFrameNumber());
        fflush(stdout);

        for (int i = 0; i < 5; i++) TEST(sb.readFrame(), "B decode frame");
        printf("  decoded 5 B frames, B frame=%d\n", sb.currentFrameNumber());
        fflush(stdout);

        TEST(sa.currentFrameNumber() == 4, "A is at frame 4");
        TEST(sb.currentFrameNumber() == 4, "B is at frame 4");

        // Seek A to frame 0, B stays at frame 4
        // seekToFrame already reads the target frame internally
        TEST(sa.seekToFrame(0), "seek A to frame 0");
        TEST(sa.currentFrameNumber() == 0, "A back to frame 0");
        TEST(sb.currentFrameNumber() == 4, "B still at frame 4 (independent)");

        sa.close();
        sb.close();
        printf("  Independent decode OK\n"); fflush(stdout);
    }

    // ============================================================
    // All passed
    // ============================================================
    printf("P6D: ALL PASS\n");
    return 0;
}
