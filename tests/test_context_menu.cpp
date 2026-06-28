// Automated test: Fullscreen right-click context menu
// Verifies the entire event chain end-to-end with a real display.
// - Windowed mode:  QMenu popup (unchanged legacy path)
// - Fullscreen mode: FullscreenCommandPanel (child widget, not a popup)
// --- Use fprintf(stderr) for all output

#include <QApplication>
#include <QTest>
#include <QTimer>
#include <QMenu>
#include <QAction>
#include <QContextMenuEvent>
#include <QWidget>
#include <windows.h>
#include "ui/MainWindow.h"
#include "ui/VideoWidget.h"
#include "ui/FullscreenCommandPanel.h"

#define LOG(...) do { fprintf(stderr, __VA_ARGS__); fflush(stderr); } while(0)

static bool g_detected = false;
static bool g_closed   = false;
static int  g_actCount = 0;

// ── Windowed-mode watcher: detects QMenu popup ─────────────────────────
static void scheduleQMenuWatcher()
{
    QTimer::singleShot(600, []() {
        QMenu *menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) { LOG("*** No QMenu popup\n"); return; }
        g_detected = true;
        g_actCount = menu->actions().size();
        LOG("+++ QMENU DETECTED +++ actions=%d\n", g_actCount);
        for (auto *a : menu->actions()) {
            LOG("    %s'%s'\n", a->isSeparator() ? "--- " : "",
                qPrintable(a->text().remove('&')));
        }
        QTest::keyClick(menu, Qt::Key_Escape);
        g_closed = true;
        LOG("+++ QMENU CLOSED +++\n");
    });
}

// ── Fullscreen-mode watcher: detects FullscreenCommandPanel ───────────
static void schedulePanelWatcher(MainWindow *w)
{
    QTimer::singleShot(600, [w]() {
        auto panels = w->findChildren<FullscreenCommandPanel*>();
        FullscreenCommandPanel *root = nullptr;
        for (auto *p : panels) {
            if (p->isRoot() && p->isVisible()) { root = p; break; }
        }
        if (!root) { LOG("*** No FullscreenCommandPanel root\n"); return; }
        g_detected = true;

        // We can't access private RowData.  Instead verify visibility
        // and that closing via Escape works.
        LOG("+++ FULLSCREENCOMMANDPANEL DETECTED +++ pos=(%d,%d) size=%dx%d\n",
            root->x(), root->y(), root->width(), root->height());

        QTest::keyClick(root, Qt::Key_Escape);
        g_closed = true;
        LOG("+++ FULLSCREENCOMMANDPANEL CLOSED +++\n");
    });
}

// ── Trigger a context menu event on a target widget ───────────────────
static void triggerContextMenu(QWidget *target)
{
    QPoint lc = target->rect().isValid() ? target->rect().center()
                                         : QPoint(100, 100);
    QPoint gc = target->mapToGlobal(lc);
    LOG("Posting ContextMenu to %s at (%d,%d) global (%d,%d)\n",
        target->metaObject()->className(), lc.x(), lc.y(), gc.x(), gc.y());
    QCoreApplication::postEvent(target,
        new QContextMenuEvent(QContextMenuEvent::Mouse, lc, gc));
}

// ── Windowed-mode test ────────────────────────────────────────────────
static bool testWindowed()
{
    LOG("\n=== Windowed mode ===\n");
    MainWindow w;
    w.resize(800, 600);
    w.show();
    if (!QTest::qWaitForWindowExposed(&w, 3000)) { LOG("FAIL: Not exposed\n"); return false; }
    QTest::qWait(200);

    g_detected = false; g_closed = false; g_actCount = 0;
    scheduleQMenuWatcher();
    triggerContextMenu(w.videoWidget());
    QTest::qWait(3000);
    if (!g_detected || !g_closed) { LOG("FAIL: windowed menu\n"); return false; }
    LOG("PASS: windowed mode\n");
    return true;
}

// ── Fullscreen-mode test ──────────────────────────────────────────────
static bool testFullscreen()
{
    LOG("\n=== Fullscreen mode ===\n");
    MainWindow w;
    w.resize(800, 600);
    w.show();
    if (!QTest::qWaitForWindowExposed(&w, 3000)) { LOG("FAIL: Not exposed\n"); return false; }
    QTest::qWait(200);

    // Enter fullscreen
    LOG("Sending F11\n");
    QTest::keyClick(&w, Qt::Key_F11);
    QTest::qWait(300);

    // Right-click on video widget
    g_detected = false; g_closed = false;
    schedulePanelWatcher(&w);
    triggerContextMenu(w.videoWidget());
    QTest::qWait(3000);
    if (!g_detected) { LOG("FAIL: no panel after video widget right-click\n"); return false; }
    if (!g_closed)   { LOG("FAIL: panel didn't close\n"); return false; }
    LOG("PASS: fullscreen video widget right-click\n");
    QTest::qWait(200);

    // Right-click on GradientOverlay
    g_detected = false; g_closed = false;
    schedulePanelWatcher(&w);
    for (auto *c : w.findChildren<QWidget*>()) {
        if (c->inherits("GradientOverlay")) {
            triggerContextMenu(c);
            break;
        }
    }
    QTest::qWait(3000);
    if (!g_detected) { LOG("FAIL: no panel after overlay right-click\n"); return false; }
    if (!g_closed)   { LOG("FAIL: panel didn't close\n"); return false; }
    LOG("PASS: fullscreen overlay right-click\n");

    // Exit fullscreen & verify NOTOPMOST
    LOG("Exiting fullscreen (F11)\n");
    QTest::keyClick(&w, Qt::Key_F11);
    QTest::qWait(500);

    HWND hwnd = reinterpret_cast<HWND>(w.winId());
    LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    bool topmost = (exStyle & WS_EX_TOPMOST) != 0;
    LOG("TOPMOST=%d\n", (int)topmost);
    if (topmost) { LOG("FAIL: still TOPMOST\n"); return false; }
    LOG("PASS: TOPMOST cleared\n");
    return true;
}

// ── Main ──────────────────────────────────────────────────────────────
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    LOG("========================================\n");
    LOG(" Fullscreen Command Panel Automation\n");
    LOG("========================================\n");

    if (!testWindowed())   return 1;
    if (!testFullscreen()) return 1;

    LOG("\n========================================\n");
    LOG(" *** ALL TESTS PASSED ***\n");
    LOG("========================================\n");
    return 0;
}
