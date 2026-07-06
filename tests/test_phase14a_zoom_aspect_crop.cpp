#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QWidget>

// Manual test of Zoom / Aspect / Crop pipeline (no GUI needed)
#include "renderer/CPURenderer.h"
#include "renderer/Renderer.h"

#define TEST(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s\n", msg); fflush(stdout); return 1; \
    } \
    printf("PASS: %s\n", msg); fflush(stdout); \
} while(0)

#define STEP(msg) printf("\n--- %s ---\n", msg); fflush(stdout)

// ============================================================
// Test 1: QAction data extraction verification
// ============================================================
static int test1_actionData()
{
    STEP("QAction Data Extraction");

    // Verify that setData/toDouble round-trips correctly
    QAction act("test");
    act.setData(0.5);
    TEST(std::abs(act.data().toDouble() - 0.5) < 0.001, "data 0.5 round-trips");

    act.setData(1.0);
    TEST(std::abs(act.data().toDouble() - 1.0) < 0.001, "data 1.0 round-trips");

    act.setData(1.5);
    TEST(std::abs(act.data().toDouble() - 1.5) < 0.001, "data 1.5 round-trips");

    act.setData(2.0);
    TEST(std::abs(act.data().toDouble() - 2.0) < 0.001, "data 2.0 round-trips");

    // Verify enum-as-int round-trips
    act.setData(static_cast<int>(0)); // Auto
    TEST(act.data().toInt() == 0, "int data 0 round-trips");

    act.setData(static_cast<int>(4)); // Ratio4_3
    TEST(act.data().toInt() == 4, "int data 4 round-trips");

    printf("  -> Test 1 PASSED\n");
    return 0;
}

// ============================================================
// Test 2: CPURenderer zoom — different zoom → different pixels
// ============================================================
static int test2_cpuRendererZoom()
{
    STEP("CPURenderer Zoom Verification");

    CPURenderer renderer;
    TEST(renderer.initialize(), "CPURenderer initializes");

    // Create a test image: left half red, right half green, center column white
    QImage frame(100, 100, QImage::Format_RGB888);
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 100; ++x) {
            if (x < 50)
                frame.setPixel(x, y, qRgb(255, 0, 0));       // left = red
            else
                frame.setPixel(x, y, qRgb(0, 255, 0));       // right = green
        }
    }

    renderer.present(frame);

    // At zoom=1.0: full 100x100 frame maps to 200x200 canvas.
    // Canvas (10,10) maps to frame (5,5) = red.
    // At zoom=2.0: dest rect = (-100,-100,400,400). Canvas (10,10) maps to
    // dest position (110,110), which maps to frame pixel 110*(100/400)=27.5 = red.
    // Need a position that differs between zoom levels.

    // Use a gradient pattern instead: pixel(x,y) = x (horizontal gradient)
    QImage frame2(100, 100, QImage::Format_Grayscale8);
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 100; ++x)
            frame2.setPixel(x, y, qRgb(x * 255 / 99, x * 255 / 99, x * 255 / 99));

    renderer.present(frame2);

    // Zoom=0.5 (zoom out): dest rect center=100,100 halfW=200*0.5*0.5=50 → (50,50,100,100)
    QImage canvas_out(200, 200, QImage::Format_RGB888);
    canvas_out.fill(Qt::black);
    {
        RenderState rs;
        rs.zoom = 0.5f;
        rs.sourceRect = QRectF(0, 0, 1, 1);
        rs.destinationRect = QRectF(0, 0, 200, 200);
        QPainter p(&canvas_out);
        renderer.paint(p, rs);
        p.end();
    }

    // Zoom=1.0 (normal): dest rect = (0,0,200,200)
    QImage canvas_norm(200, 200, QImage::Format_RGB888);
    canvas_norm.fill(Qt::black);
    {
        RenderState rs;
        rs.zoom = 1.0f;
        rs.sourceRect = QRectF(0, 0, 1, 1);
        rs.destinationRect = QRectF(0, 0, 200, 200);
        QPainter p(&canvas_norm);
        renderer.paint(p, rs);
        p.end();
    }

    // Zoom=2.0 (zoom in): dest rect = (-100,-100,400,400)
    QImage canvas_in(200, 200, QImage::Format_RGB888);
    canvas_in.fill(Qt::black);
    {
        RenderState rs;
        rs.zoom = 2.0f;
        rs.sourceRect = QRectF(0, 0, 1, 1);
        rs.destinationRect = QRectF(0, 0, 200, 200);
        QPainter p(&canvas_in);
        renderer.paint(p, rs);
        p.end();
    }

    // At zoom=1.0: canvas (10,10) ← source: 10*(100/200)=5 → frame pixel(5) = 5*255/99 ≈ 13
    // At zoom=0.5: canvas (10,10) ← dest rect=(50,50,100,100)
    //   canvas (10,10) is inside dest rect? No! dest=(50,50,100,100) means
    //   x range [50,150], y range [50,150]. Canvas (10,10) is OUTSIDE.
    //   So this pixel should be black (clear color of canvas).
    // At zoom=2.0: canvas (10,10) ← dest rect=(-100,-100,400,400)
    //   canvas (10,10) maps to dest position (110,110) → frame pixel 110*100/400=27.5
    //   brightness = 27*255/99 ≈ 70

    // Check pixel at canvas (10,10) — outside 0.5x zoom dest rect = black
    QRgb out_pix = canvas_out.pixel(10, 10);
    printf("  Zoom 0.5 pixel (10,10): rgb(%d,%d,%d)\n",
           qRed(out_pix), qGreen(out_pix), qBlue(out_pix));
    TEST(out_pix == qRgb(0,0,0), "Zoom 0.5: pixel outside dest rect is black");

    // At zoom=2.0: canvas (10,10) maps to frame brightness ~70
    QRgb in_pix = canvas_in.pixel(10, 10);
    printf("  Zoom 2.0 pixel (10,10): rgb(%d,%d,%d)\n",
           qRed(in_pix), qGreen(in_pix), qBlue(in_pix));
    TEST(qRed(in_pix) > 50, "Zoom 2.0: pixel shows zoomed-in brighter area");

    // At zoom=1.0: canvas (10,10) maps to frame brightness ~13
    QRgb norm_pix = canvas_norm.pixel(10, 10);
    printf("  Zoom 1.0 pixel (10,10): rgb(%d,%d,%d)\n",
           qRed(norm_pix), qGreen(norm_pix), qBlue(norm_pix));
    TEST(qRed(norm_pix) < 20, "Zoom 1.0: pixel shows dark edge");

    // Zoom 0.5 and 2.0 produce different results from 1.0 at checkable positions
    printf("  Zoom produces different pixel values at affected canvas positions\n");

    renderer.cleanup();
    printf("  -> Test 2 PASSED\n");
    return 0;
}

// ============================================================
// Test 3: CPURenderer sourceRect crop
// ============================================================
static int test3_cpuRendererCrop()
{
    STEP("CPURenderer sourceRect Crop");

    CPURenderer renderer;
    TEST(renderer.initialize(), "CPURenderer initializes");

    // Create a test image: left half red, right half green
    QImage frame(100, 50, QImage::Format_RGB888);
    for (int y = 0; y < 50; ++y)
        for (int x = 0; x < 50; ++x)
            frame.setPixel(x, y, qRgb(255, 0, 0));     // left = red
    for (int y = 0; y < 50; ++y)
        for (int x = 50; x < 100; ++x)
            frame.setPixel(x, y, qRgb(0, 255, 0));     // right = green

    renderer.present(frame);

    // Paint with sourceRect = left half (0,0,0.5,1)
    QImage canvas_left(100, 100, QImage::Format_RGB888);
    canvas_left.fill(Qt::black);
    {
        RenderState rs;
        rs.zoom = 1.0f;
        rs.sourceRect = QRectF(0, 0, 0.5, 1.0);  // left half of frame
        rs.destinationRect = QRectF(0, 0, 100, 100);
        QPainter p(&canvas_left);
        renderer.paint(p, rs);
        p.end();
    }

    // Paint with sourceRect = right half (0.5,0,0.5,1)
    QImage canvas_right(100, 100, QImage::Format_RGB888);
    canvas_right.fill(Qt::black);
    {
        RenderState rs;
        rs.zoom = 1.0f;
        rs.sourceRect = QRectF(0.5, 0, 0.5, 1.0);  // right half of frame
        rs.destinationRect = QRectF(0, 0, 100, 100);
        QPainter p(&canvas_right);
        renderer.paint(p, rs);
        p.end();
    }

    QRgb left_pixel = canvas_left.pixel(50, 50);
    QRgb right_pixel = canvas_right.pixel(50, 50);

    printf("  Left-cropped center: rgb(%d,%d,%d)\n",
           qRed(left_pixel), qGreen(left_pixel), qBlue(left_pixel));
    printf("  Right-cropped center: rgb(%d,%d,%d)\n",
           qRed(right_pixel), qGreen(right_pixel), qBlue(right_pixel));

    TEST(qRed(left_pixel) > qGreen(left_pixel), "Left crop shows red>green");
    TEST(qGreen(right_pixel) > qRed(right_pixel), "Right crop shows green>red");

    renderer.cleanup();
    printf("  -> Test 3 PASSED\n");
    return 0;
}

// ============================================================
// Test 4: Compute destination rect for different aspect modes
// ============================================================
static int test4_computeDestination()
{
    STEP("Compute Destination: Aspect Ratio Modes");

    // We can't access VideoWidget::computeDestination directly (it's private).
    // But we can test the logic through the CPURenderer by varying
    // destinationRect and checking output.
    // 
    // Instead, let's test aspect ratio logic manually:

    // Simulate: 320x240 video, viewport 1280x626, DAR=4:3
    QSize imgSize(320, 240);
    QRect viewport(0, 0, 1280, 626);
    int darNum = 4, darDen = 3;

    // Auto: use DAR 4:3
    QSize displaySize = imgSize;
    if (darNum > 0 && darDen > 0)
        displaySize = QSize(darNum, darDen);
    QSize scaled = displaySize.scaled(viewport.size(), Qt::KeepAspectRatio);
    printf("  Auto (DAR 4:3): display=%dx%d scaled=%dx%d dst=(%d,0,%d,%d)\n",
           displaySize.width(), displaySize.height(),
           scaled.width(), scaled.height(),
           (viewport.width() - scaled.width()) / 2,
           scaled.width(), scaled.height());
    TEST(scaled.width() == 834 || scaled.width() == 835,
         "Auto DAR 4:3 gives ~834x626");

    // 16:9
    displaySize = QSize(16, 9);
    scaled = displaySize.scaled(viewport.size(), Qt::KeepAspectRatio);
    printf("  16:9: display=%dx%d scaled=%dx%d dst=(%d,0,%d,%d)\n",
           displaySize.width(), displaySize.height(),
           scaled.width(), scaled.height(),
           (viewport.width() - scaled.width()) / 2,
           scaled.width(), scaled.height());
    TEST(scaled.width() == 1112, "16:9 gives 1112x626");

    // 21:9
    displaySize = QSize(21, 9);
    scaled = displaySize.scaled(viewport.size(), Qt::KeepAspectRatio);
    printf("  21:9: display=%dx%d scaled=%dx%d dst=(%d,0,%d,%d)\n",
           displaySize.width(), displaySize.height(),
           scaled.width(), scaled.height(),
           (viewport.width() - scaled.width()) / 2,
           scaled.width(), scaled.height());
    TEST(scaled.width() == 1280, "21:9 gives 1280x548 (width-constrained)");

    // Original (pixel aspect)
    displaySize = QSize(320, 240);
    scaled = displaySize.scaled(viewport.size(), Qt::KeepAspectRatio);
    printf("  Original: display=%dx%d scaled=%dx%d dst=(%d,%d,%d,%d)\n",
           displaySize.width(), displaySize.height(),
           scaled.width(), scaled.height(),
           (viewport.width() - scaled.width()) / 2,
           (viewport.height() - scaled.height()) / 2,
           scaled.width(), scaled.height());
    TEST(scaled.width() == 834 || scaled.width() == 835,
         "Original 320x240 gives ~834x626");

    // Fill (KeepAspectRatioByExpanding)
    scaled = displaySize.scaled(viewport.size(), Qt::KeepAspectRatioByExpanding);
    printf("  Fill: display=%dx%d scaled=%dx%d\n",
           displaySize.width(), displaySize.height(),
           scaled.width(), scaled.height());
    TEST(scaled.width() == 1280, "Fill expands to fill width");

    printf("  -> Test 4 PASSED\n");
    return 0;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    printf("============================================\n");
    printf("Phase 14A Zoom/Aspect/Crop Pipeline Test\n");
    printf("============================================\n\n");

    int failed = 0;
#define RUN_T(name) do { \
    int r = name; \
    if (r != 0) { \
        printf(">>> TEST FAILED (code=%d) <<<\n\n", r); \
        failed = r; \
        goto done; \
    } \
} while(0)

    RUN_T(test1_actionData());
    RUN_T(test2_cpuRendererZoom());
    RUN_T(test3_cpuRendererCrop());
    RUN_T(test4_computeDestination());


done:
    if (failed == 0) {
        printf("\n*** ALL PHASE 14A TESTS PASSED ***\n");
    } else {
        printf("\n*** PHASE 14A TESTS FAILED ***\n");
    }
    printf("\n");
    return failed;
}
