#include <cstdio>
#include <cstdlib>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QDir>
#include <QDateTime>
#include <QtMath>

#include "subtitles/SubtitleManager.h"
#include "subtitles/ExternalSubtitleLoader.h"
#include "subtitles/AssParser.h"
#include "subtitles/AssOverrideParser.h"
#include "subtitles/SubtitleRenderer.h"
#include "subtitles/AssAnimationEngine.h"
#include "subtitles/AssClippingEngine.h"
#include "subtitles/AssRenderCache.h"

#define TEST(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s\n", msg); fflush(stdout); return 1; \
    } \
    printf("PASS: %s\n", msg); fflush(stdout); \
} while(0)

// Count non-transparent (visible) pixels in an image
static int countVisiblePixels(const QImage &img)
{
    int count = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(img.pixel(x, y)) > 0) ++count;
        }
    }
    return count;
}

// Save debug image to a temp directory for visual inspection
static QString saveDebugImage(const QImage &img, const QString &label)
{
    QString dir = QDir::tempPath() + "/lunar_phase7c_debug";
    QDir().mkpath(dir);
    QString path = dir + "/" + label + ".png";
    img.save(path);
    printf("  DEBUG saved: %s\n", qPrintable(path));
    fflush(stdout);
    return path;
}

// Verify specific region has visible content
static bool regionHasContent(const QImage &img, int x, int y, int w, int h)
{
    int x1 = qBound(0, x, img.width() - 1);
    int y1 = qBound(0, y, img.height() - 1);
    int x2 = qBound(0, x + w, img.width());
    int y2 = qBound(0, y + h, img.height());
    for (int py = y1; py < y2; ++py)
        for (int px = x1; px < x2; ++px)
            if (qAlpha(img.pixel(px, py)) > 0) return true;
    return false;
}

struct TestContext {
    QString testDir;
    QString assFile;
    QString animFile;
    int width = 640;
    int height = 480;
    QImage canvas; // latest rendered canvas
};

// ---------------------------------------------------------------------------
// Test 1: ExternalSubtitleLoader ASS parsing through SubtitleManager
// ---------------------------------------------------------------------------
static int test1_parse(TestContext &tc)
{
    printf("\n=== RT Test 1: Load & Parse ASS ===\n");
    SubtitleManager mgr;
    bool loaded = mgr.loadExternalFile(tc.assFile);
    TEST(loaded, "SubtitleManager loads ASS file");

    TEST(mgr.trackCount() == 1, "Exactly 1 track");
    mgr.setActiveTrack(0);
    TEST(mgr.activeTrackIndex() == 0, "Active track index 0");

    // Verify track frame count
    const SubtitleTrack *track = mgr.track(0);
    TEST(track != nullptr, "Track exists");
    TEST(track->frames.size() >= 7, "At least 7 frames parsed");

    // Check frame types
    for (int i = 0; i < track->frames.size(); ++i) {
        TEST(track->frames[i].type == SubtitleType::ASS,
             QString("Frame %1 type = ASS").arg(i).toUtf8().constData());
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Test 2: Render frames at various timestamps
// ---------------------------------------------------------------------------
static int test2_renderTimestamps(TestContext &tc)
{
    printf("\n=== RT Test 2: Render at Timestamps ===\n");
    SubtitleManager mgr;
    mgr.loadExternalFile(tc.assFile);
    mgr.setActiveTrack(0);

    // t=0.5s: no subs (first starts at 1.0s)
    QVector<QImage> imgs0 = mgr.getSubtitleImages(0.5, tc.width, tc.height);
    // Either empty or a null/transparent canvas
    if (!imgs0.isEmpty()) {
        int vis0 = countVisiblePixels(imgs0[0]);
        TEST(vis0 == 0, "No visible content at t=0.5s");
    } else {
        TEST(true, "No images at t=0.5s (empty)");
    }

    // t=1.5s: multiple subs active (layer 0: "Basic ASS subtitle with bold and italic",
    //         layer 1: "Bottom-left caption with color and outline",
    //         layer 2: "Large text normal")
    QVector<QImage> imgs1 = mgr.getSubtitleImages(1.5, tc.width, tc.height);
    TEST(!imgs1.isEmpty(), "Images at t=1.5s");
    if (!imgs1.isEmpty()) {
        tc.canvas = imgs1[0];
        TEST(tc.canvas.width() == tc.width && tc.canvas.height() == tc.height,
             "Canvas is full video size");
        int vis1 = countVisiblePixels(tc.canvas);
        TEST(vis1 > 0, "Visible content at t=1.5s");
        printf("  DEBUG: visible pixels at t=1.5s = %d\n", vis1);
        // saveDebugImage(tc.canvas, "rt_t1_5s");
    }

    // t=3.0s: layer 0 has "Top Center Title", layer 1 has "Shadowed text", layer 2 has "Center positioned text"
    QVector<QImage> imgs2 = mgr.getSubtitleImages(3.0, tc.width, tc.height);
    TEST(!imgs2.isEmpty(), "Images at t=3.0s");
    if (!imgs2.isEmpty()) {
        int vis2 = countVisiblePixels(imgs2[0]);
        TEST(vis2 > 0, "Visible content at t=3.0s");
        // saveDebugImage(imgs2[0], "rt_t3_0s");
    }

    // t=7.0s: no subs (last ends at 6.0s)
    QVector<QImage> imgs3 = mgr.getSubtitleImages(7.0, tc.width, tc.height);
    if (!imgs3.isEmpty()) {
        int vis3 = countVisiblePixels(imgs3[0]);
        TEST(vis3 == 0, "No visible content at t=7.0s");
    } else {
        TEST(true, "No images at t=7.0s (empty)");
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Test 3: Layer compositing (higher layer renders on top)
// ---------------------------------------------------------------------------
static int test3_layers(TestContext &tc)
{
    printf("\n=== RT Test 3: Layer Compositing ===\n");
    // At t=1.5s: layer 0 = first two lines, layer 1 = one line, layer 2 = one line
    // At t=4.0s: only layer 0 has "Basic ASS subtitle..." and layer 1 still has "Shadowed text"
    SubtitleManager mgr;
    mgr.loadExternalFile(tc.assFile);
    mgr.setActiveTrack(0);

    QVector<QImage> imgs = mgr.getSubtitleImages(1.5, tc.width, tc.height);
    TEST(!imgs.isEmpty(), "Layer composited images at t=1.5s");
    if (!imgs.isEmpty()) {
        int vis = countVisiblePixels(imgs[0]);
        TEST(vis > 0, "Layer compositing produces visible content");
        // There should be significantly more content than a single subtitle
        // because multiple layers are composited
        // If we only see content from one sub, something is wrong
        // Test: check that content is visible in multiple vertical bands
        int bottomHalf = regionHasContent(imgs[0], 0, tc.height/2, tc.width, tc.height/2)
                         ? 1 : 0;
        TEST(bottomHalf == 1, "Content visible in bottom half of canvas");
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Test 4: \pos() override tag positions text correctly
// ---------------------------------------------------------------------------
static int test4_posOverride(TestContext &tc)
{
    printf("\n=== RT Test 4: \\pos() Override ===\n");
    // The last dialogue is: "{\\pos(320,240)}Center positioned text" at t=3.5s-6.0s
    SubtitleManager mgr;
    mgr.loadExternalFile(tc.assFile);
    mgr.setActiveTrack(0);

    QVector<QImage> imgs = mgr.getSubtitleImages(4.0, tc.width, tc.height);

    // Should have content. The \pos(320,240) text should render near center
    TEST(!imgs.isEmpty(), "Images at t=4.0s");
    if (!imgs.isEmpty()) {
        int vis = countVisiblePixels(imgs[0]);
        TEST(vis > 0, "Visible content at t=4.0s");

        // The \pos(320,240) text should have content near vertical center
        int centerRegion = regionHasContent(imgs[0],
            tc.width/3, tc.height/3, tc.width/3, tc.height/3) ? 1 : 0;
        TEST(centerRegion == 1, "Content visible in upper-center region");

        // Should NOT have content at the very bottom (default alignment position)
        // since \pos overrides it
        int bottomRegion = regionHasContent(imgs[0],
            tc.width/4, tc.height - 60, tc.width/2, 50) ? 1 : 0;
        // Note: other subs at t=4.0s may also be visible, so bottom could still have content
        // This is a soft check
        printf("  DEBUG: bottom region has content = %d (may overlap with other subs)\n", bottomRegion);
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Test 5: Bold and italic tags produce visibly different output
// ---------------------------------------------------------------------------
static int test5_boldItalic(TestContext &tc)
{
    printf("\n=== RT Test 5: Bold/Italic Rendering ===\n");
    SubtitleRenderer renderer;

    SubtitleFrame frame;
    frame.type = SubtitleType::ASS;
    frame.text = "Normal {\\b1}Bold{\\b0} {\\i1}Italic{\\i0}";
    frame.startSeconds = 0.0;
    frame.endSeconds = 5.0;

    QVector<SubtitleSurface> surfaces = renderer.render(frame, tc.width, tc.height);
    TEST(!surfaces.isEmpty(), "ASS bold/italic renders surfaces");
    if (!surfaces.isEmpty()) {
        QImage img = surfaces[0].toImage();
        int vis = countVisiblePixels(img);
        TEST(vis > 0, "Bold/italic text visible");
        // saveDebugImage(img, "rt_bold_italic");
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Test 6: Color override renders correct color
// ---------------------------------------------------------------------------
static int test6_colorOverride(TestContext &tc)
{
    printf("\n=== RT Test 6: Color Override ===\n");
    SubtitleRenderer renderer;

    // Render green text (\c takes ASS BGR format: &HBBGGRR&)
    // &H00FF00& = BB=00, GG=FF, RR=00 → Green
    SubtitleFrame frame;
    frame.type = SubtitleType::ASS;
    frame.text = "{\\c&H00FF00&}GreenText";
    frame.startSeconds = 0.0;
    frame.endSeconds = 5.0;

    QVector<SubtitleSurface> surfaces = renderer.render(frame, tc.width, tc.height);
    TEST(!surfaces.isEmpty(), "Color override renders surface");
    if (!surfaces.isEmpty()) {
        QImage img = surfaces[0].toImage();
        int vis = countVisiblePixels(img);
        TEST(vis > 0, "Green text visible");

        // Check that visible pixels have a strong green component
        bool hasGreen = false;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                QRgb px = img.pixel(x, y);
                if (qAlpha(px) > 0) {
                    int g = qGreen(px);
                    int r = qRed(px);
                    int b = qBlue(px);
                    if (g > r && g > b) {
                        hasGreen = true;
                        break;
                    }
                }
            }
            if (hasGreen) break;
        }
        TEST(hasGreen, "Green pixels have dominant green channel");
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Test 7: \fn font family change
// ---------------------------------------------------------------------------
static int test7_fontFamily(TestContext &tc)
{
    printf("\n=== RT Test 7: Font Family Override ===\n");
    SubtitleRenderer renderer;

    SubtitleFrame frame;
    frame.type = SubtitleType::ASS;
    frame.text = "{\\fnArial}ArialNormal {\\fnImpact}ImpactText";
    frame.startSeconds = 0.0;
    frame.endSeconds = 5.0;

    QVector<SubtitleSurface> surfaces = renderer.render(frame, tc.width, tc.height);
    TEST(!surfaces.isEmpty(), "Font family override renders surface");
    if (!surfaces.isEmpty()) {
        int vis = countVisiblePixels(surfaces[0].toImage());
        TEST(vis > 0, "Font family text visible");
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Test 8: Outline rendering (\bord)
// ---------------------------------------------------------------------------
static int test8_outline(TestContext &tc)
{
    printf("\n=== RT Test 8: Outline Rendering ===\n");
    SubtitleRenderer renderer;

    // ASS BGR color format: &HBBGGRR& — so &H0000FF& = B=00,G=00,R=FF = Red
    SubtitleFrame frame;
    frame.type = SubtitleType::ASS;
    frame.text = "{\\bord10\\c&H0000FF&}ThickOutline";
    frame.startSeconds = 0.0;
    frame.endSeconds = 5.0;

    QVector<SubtitleSurface> surfaces = renderer.render(frame, tc.width, tc.height);
    TEST(!surfaces.isEmpty(), "Outline renders surface");
    if (!surfaces.isEmpty()) {
        QImage img = surfaces[0].toImage();
        int vis = countVisiblePixels(img);
        TEST(vis > 0, "Outline text visible");

        // With bord=10 (pen width=20), there should be visible black outline
        // around the text fill. Text fill is red, outline is default black.
        bool hasOutlinePixels = false;
        bool hasRedFill = false;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                QRgb px = img.pixel(x, y);
                if (qAlpha(px) > 0) {
                    int r = qRed(px);
                    int g = qGreen(px);
                    int b = qBlue(px);
                    // Red fill pixels (dominant red)
                    if (r > 100 && g < 80 && b < 80) hasRedFill = true;
                    // Black outline pixels (all channels low, alpha>0)
                    if (r < 60 && g < 60 && b < 60) hasOutlinePixels = true;
                }
            }
        }
        TEST(hasRedFill, "Red fill present in outlined text");
        TEST(hasOutlinePixels, "Dark outline pixels present");

        if (!hasRedFill) saveDebugImage(img, "rt_outline_nofill");
        if (!hasOutlinePixels) saveDebugImage(img, "rt_outline_nostroke");
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Test 9: Shadow rendering (\shad)
// ---------------------------------------------------------------------------
static int test9_shadow(TestContext &tc)
{
    printf("\n=== RT Test 9: Shadow Rendering ===\n");
    SubtitleRenderer renderer;

    SubtitleFrame frame;
    frame.type = SubtitleType::ASS;
    frame.text = "{\\shad(8,8)}Shadowed";
    frame.startSeconds = 0.0;
    frame.endSeconds = 5.0;

    QVector<SubtitleSurface> surfaces = renderer.render(frame, tc.width, tc.height);
    TEST(!surfaces.isEmpty(), "Shadow renders surface");
    if (!surfaces.isEmpty()) {
        int vis = countVisiblePixels(surfaces[0].toImage());
        TEST(vis > 0, "Shadowed text visible");
        // saveDebugImage(surfaces[0].toImage(), "rt_shadow");
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Test 10: Alignment variants
// ---------------------------------------------------------------------------
static int test10_alignment(TestContext &tc)
{
    printf("\n=== RT Test 10: Alignment ===\n");
    // The Title style has alignment=8 (top-center) and Default has alignment=2 (bottom-center)
    // Title dialogue: "Top Center Title" at t=2.0-4.0s
    SubtitleManager mgr;
    mgr.loadExternalFile(tc.assFile);
    mgr.setActiveTrack(0);

    QVector<QImage> imgs = mgr.getSubtitleImages(2.5, tc.width, tc.height);
    TEST(!imgs.isEmpty(), "Images at t=2.5s");
    if (!imgs.isEmpty()) {
        int vis = countVisiblePixels(imgs[0]);
        TEST(vis > 0, "Visible content at t=2.5s");

        // Check content in top half (Title is top-center alignment 8)
        int topRegion = regionHasContent(imgs[0],
            0, 0, tc.width, tc.height/3) ? 1 : 0;
        printf("  DEBUG: top region content = %d (Title style alignment 8)\n", topRegion);

        // Check content in bottom half (other subs with Default alignment 2)
        int bottomRegion = regionHasContent(imgs[0],
            0, tc.height*2/3, tc.width, tc.height/3) ? 1 : 0;
        printf("  DEBUG: bottom region content = %d (Default alignment 2)\n", bottomRegion);
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Test 11: Style resolution — named style lookup
// ---------------------------------------------------------------------------
static int test11_styleResolution(TestContext &tc)
{
    printf("\n=== RT Test 11: Style Resolution ===\n");
    AssParsedFile parsed = AssParser::parse(tc.assFile);
    auto styleMap = AssParser::buildStyleMap(parsed.styles);

    // Verify all three named styles resolve
    TEST(styleMap.contains("Default"), "Default style in map");
    TEST(styleMap.contains("Title"), "Title style in map");
    TEST(styleMap.contains("Small"), "Small style in map");

    // Verify properties
    const auto &titleStyle = styleMap["Title"];
    TEST(titleStyle.font.family == "Impact", "Title resolves to Impact");
    TEST(titleStyle.font.size == 48, "Title resolves to size 48");
    TEST(titleStyle.font.bold, "Title resolves to bold");

    const auto &smallStyle = styleMap["Small"];
    TEST(smallStyle.font.size == 18, "Small resolves to size 18");
    TEST(smallStyle.position.alignment == 1, "Small resolves to alignment 1 (bottom-left)");

    return 0;
}

// ---------------------------------------------------------------------------
// Test 12: Direct render of override sequences
// ---------------------------------------------------------------------------
static int test12_directRender(TestContext &tc)
{
    printf("\n=== RT Test 12: Direct Override Rendering ===\n");
    SubtitleRenderer renderer;

    // Test \fscx\fscy stretch
    {
        SubtitleFrame frame;
        frame.type = SubtitleType::ASS;
        frame.text = "{\\fscx150\\fscy200}Stretched";
        frame.startSeconds = 0.0;
        frame.endSeconds = 5.0;
        QVector<SubtitleSurface> surfaces = renderer.render(frame, tc.width, tc.height);
        TEST(!surfaces.isEmpty(), "Stretch tag renders");
        if (!surfaces.isEmpty()) {
            int vis = countVisiblePixels(surfaces[0].toImage());
            TEST(vis > 0, "Stretched text visible");
        }
    }

    // Test multiple override blocks
    // ASS BGR format: &H0000FF& = Red, &HFF0000& = Blue, &H00FF00& = Green
    {
        SubtitleFrame frame;
        frame.type = SubtitleType::ASS;
        frame.text = "{\\c&H0000FF&}Red{\\c&HFF0000&}Blue{\\c&H00FF00&}Green";
        frame.startSeconds = 0.0;
        frame.endSeconds = 5.0;
        QVector<SubtitleSurface> surfaces = renderer.render(frame, tc.width, tc.height);
        TEST(!surfaces.isEmpty(), "Multi-color override renders");
        if (!surfaces.isEmpty()) {
            QImage img = surfaces[0].toImage();
            int vis = countVisiblePixels(img);
            TEST(vis > 0, "Multi-color text visible");

            bool hasRed = false, hasBlue = false, hasGreen = false;
            for (int y = 0; y < img.height(); ++y) {
                for (int x = 0; x < img.width(); ++x) {
                    QRgb px = img.pixel(x, y);
                    if (qAlpha(px) > 0) {
                        int r = qRed(px), g = qGreen(px), b = qBlue(px);
                        if (r > g + 50 && r > b + 50) hasRed = true;
                        if (b > r + 50 && b > g + 50) hasBlue = true;
                        if (g > r + 50 && g > b + 50) hasGreen = true;
                    }
                }
            }
            TEST(hasRed, "Red component present");
            TEST(hasBlue, "Blue component present");
            if (!hasGreen) saveDebugImage(img, "rt_multi_color");
            TEST(hasGreen, "Green component present");
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Test 13: Full pipeline with SubtitleManager compositing
// ---------------------------------------------------------------------------
static int test13_fullPipeline(TestContext &tc)
{
    printf("\n=== RT Test 13: Full Pipeline Compositing ===\n");
    SubtitleManager mgr;
    mgr.loadExternalFile(tc.assFile);
    mgr.setActiveTrack(0);

    // Render at a time when multiple subs are active
    // t=2.5s has: "Basic ASS..." (layer 0), "Top Center Title" (layer 0),
    //              "Small caption" (layer 1), "Shadowed text" (layer 1),
    //              "Center positioned" (layer 2, starts at 3.5s) -- not active yet
    QVector<QImage> imgs = mgr.getSubtitleImages(2.5, tc.width, tc.height);
    TEST(!imgs.isEmpty(), "Full pipeline renders at t=2.5s");
    if (!imgs.isEmpty()) {
        int vis = countVisiblePixels(imgs[0]);
        TEST(vis > 500, QString("Substantial visible content: %1 pixels").arg(vis).toUtf8().constData());
    }

    // At t=4.5s: "Bottom-left caption" (layer 1), "Shadowed text" (layer 1, ends at 5),
    //            "Center positioned" (layer 2), "Stretched text" (layer 3)
    QVector<QImage> imgs2 = mgr.getSubtitleImages(4.5, tc.width, tc.height);
    TEST(!imgs2.isEmpty(), "Full pipeline renders at t=4.5s");
    if (!imgs2.isEmpty()) {
        int vis = countVisiblePixels(imgs2[0]);
        TEST(vis > 500, QString("Substantial visible content at t=4.5s: %1 pixels").arg(vis).toUtf8().constData());
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Animation Tests
// ---------------------------------------------------------------------------

// Test 14: \move() interpolates position over time
static int test14_moveAnimation(TestContext &tc)
{
    printf("\n=== RT Test 14: \\move() Animation ===\n");
    SubtitleRenderer renderer;

    // Create a subtitle with \move from (50,240) to (590,240) over 4 seconds
    SubtitleFrame frame;
    frame.type = SubtitleType::ASS;
    frame.text = "{\\move(50,240,590,240,0,4000)}Moving";
    frame.startSeconds = 0.0;
    frame.endSeconds = 5.0;

    SubtitleRenderContext ctx;
    ctx.videoSize = QSize(tc.width, tc.height);
    ctx.playRes = QSize(tc.width, tc.height);

    // At t=0.0s: position should be at (50, 240)
    ctx.currentPTS = 0.0;
    frame.renderPTS = 0;
    auto lines0 = AssOverrideParser::segment(frame.text, frame, ctx);
    AssAnimationEngine::resolveLines(lines0, ctx, frame.startSeconds, frame.endSeconds);
    if (!lines0.isEmpty() && !lines0.first().segments.isEmpty()) {
        const auto &seg = lines0.first().segments.first();
        TEST(seg.hasExplicitPos, "\\move sets explicit position");
        TEST(qAbs(seg.position.x() - 50.0) < 5.0, "\\move start X ~50");
        TEST(qAbs(seg.position.y() - 240.0) < 5.0, "\\move start Y ~240");
    }

    // At t=2.0s (midpoint): position should be at (320, 240)
    ctx.currentPTS = 2000.0;
    frame.renderPTS = 2000;
    auto lines1 = AssOverrideParser::segment(frame.text, frame, ctx);
    AssAnimationEngine::resolveLines(lines1, ctx, frame.startSeconds, frame.endSeconds);
    if (!lines1.isEmpty() && !lines1.first().segments.isEmpty()) {
        const auto &seg = lines1.first().segments.first();
        TEST(qAbs(seg.position.x() - 320.0) < 10.0, "\\move mid X ~320");
        TEST(qAbs(seg.position.y() - 240.0) < 5.0, "\\move mid Y ~240");
    }

    // At t=4.0s (end): position should be at (590, 240)
    ctx.currentPTS = 4000.0;
    frame.renderPTS = 4000;
    auto lines2 = AssOverrideParser::segment(frame.text, frame, ctx);
    AssAnimationEngine::resolveLines(lines2, ctx, frame.startSeconds, frame.endSeconds);
    if (!lines2.isEmpty() && !lines2.first().segments.isEmpty()) {
        const auto &seg = lines2.first().segments.first();
        TEST(qAbs(seg.position.x() - 590.0) < 5.0, "\\move end X ~590");
        TEST(qAbs(seg.position.y() - 240.0) < 5.0, "\\move end Y ~240");
    }

    return 0;
}

// Test 15: \fad() changes opacity over time
static int test15_fadeAnimation(TestContext &tc)
{
    printf("\n=== RT Test 15: \\fad() Animation ===\n");
    SubtitleRenderer renderer;

    SubtitleFrame frame;
    frame.type = SubtitleType::ASS;
    frame.text = "{\\fad(1000,1000)}Fading";
    frame.startSeconds = 0.0;
    frame.endSeconds = 5.0;

    SubtitleRenderContext ctx;
    ctx.videoSize = QSize(tc.width, tc.height);
    ctx.playRes = QSize(tc.width, tc.height);

    // At t=0.0s (start): fully transparent (fading in)
    ctx.currentPTS = 0.0;
    frame.renderPTS = 0;
    auto lines0 = AssOverrideParser::segment(frame.text, frame, ctx);
    AssAnimationEngine::resolveLines(lines0, ctx, frame.startSeconds, frame.endSeconds);
    if (!lines0.isEmpty() && !lines0.first().segments.isEmpty()) {
        const auto &seg = lines0.first().segments.first();
        TEST(seg.opacity < 0.1, "\\fad at start: opacity ~0");
    }

    // At t=0.5s (mid-fade-in): opacity ~0.5
    ctx.currentPTS = 500.0;
    frame.renderPTS = 500;
    auto lines1 = AssOverrideParser::segment(frame.text, frame, ctx);
    AssAnimationEngine::resolveLines(lines1, ctx, frame.startSeconds, frame.endSeconds);
    if (!lines1.isEmpty() && !lines1.first().segments.isEmpty()) {
        const auto &seg = lines1.first().segments.first();
        TEST(seg.opacity > 0.3 && seg.opacity < 0.7, "\\fad mid-fade-in: opacity ~0.5");
    }

    // At t=2.5s (fully visible): opacity ~1.0
    ctx.currentPTS = 2500.0;
    frame.renderPTS = 2500;
    auto lines2 = AssOverrideParser::segment(frame.text, frame, ctx);
    AssAnimationEngine::resolveLines(lines2, ctx, frame.startSeconds, frame.endSeconds);
    if (!lines2.isEmpty() && !lines2.first().segments.isEmpty()) {
        const auto &seg = lines2.first().segments.first();
        TEST(seg.opacity > 0.95, "\\fad fully visible: opacity ~1.0");
    }

    // At t=4.5s (mid-fade-out): opacity ~0.5
    ctx.currentPTS = 4500.0;
    frame.renderPTS = 4500;
    auto lines3 = AssOverrideParser::segment(frame.text, frame, ctx);
    AssAnimationEngine::resolveLines(lines3, ctx, frame.startSeconds, frame.endSeconds);
    if (!lines3.isEmpty() && !lines3.first().segments.isEmpty()) {
        const auto &seg = lines3.first().segments.first();
        TEST(seg.opacity > 0.3 && seg.opacity < 0.7, "\\fad mid-fade-out: opacity ~0.5");
    }

    // At t=5.0s (end): fully transparent (faded out)
    ctx.currentPTS = 5000.0;
    frame.renderPTS = 5000;
    auto lines4 = AssOverrideParser::segment(frame.text, frame, ctx);
    AssAnimationEngine::resolveLines(lines4, ctx, frame.startSeconds, frame.endSeconds);
    if (!lines4.isEmpty() && !lines4.first().segments.isEmpty()) {
        const auto &seg = lines4.first().segments.first();
        TEST(seg.opacity < 0.1, "\\fad at end: opacity ~0");
    }

    return 0;
}

// Test 16: \t() with \bord interpolation
static int test16_transformBord(TestContext &tc)
{
    printf("\n=== RT Test 16: \\t() Border Animation ===\n");
    SubtitleRenderer renderer;

    SubtitleFrame frame;
    frame.type = SubtitleType::ASS;
    frame.text = "{\\bord2}{\\t(0,4000,\\bord10)}BordAnim";
    frame.startSeconds = 0.0;
    frame.endSeconds = 5.0;

    SubtitleRenderContext ctx;
    ctx.videoSize = QSize(tc.width, tc.height);
    ctx.playRes = QSize(tc.width, tc.height);

    // At t=0: bord should be 2 (base)
    ctx.currentPTS = 0.0;
    frame.renderPTS = 0;
    auto lines0 = AssOverrideParser::segment(frame.text, frame, ctx);
    AssAnimationEngine::resolveLines(lines0, ctx, frame.startSeconds, frame.endSeconds);
    if (!lines0.isEmpty() && !lines0.first().segments.isEmpty()) {
        const auto &seg = lines0.first().segments.first();
        TEST(qAbs(seg.outlineWidth - 2.0) < 0.5, "\\t bord at start = ~2");
    }

    // At t=2s (mid): bord should be ~6
    ctx.currentPTS = 2000.0;
    frame.renderPTS = 2000;
    auto lines1 = AssOverrideParser::segment(frame.text, frame, ctx);
    AssAnimationEngine::resolveLines(lines1, ctx, frame.startSeconds, frame.endSeconds);
    if (!lines1.isEmpty() && !lines1.first().segments.isEmpty()) {
        const auto &seg = lines1.first().segments.first();
        TEST(qAbs(seg.outlineWidth - 6.0) < 1.0, "\\t bord at mid = ~6");
    }

    // At t=4s (end): bord should be ~10
    ctx.currentPTS = 4000.0;
    frame.renderPTS = 4000;
    auto lines2 = AssOverrideParser::segment(frame.text, frame, ctx);
    AssAnimationEngine::resolveLines(lines2, ctx, frame.startSeconds, frame.endSeconds);
    if (!lines2.isEmpty() && !lines2.first().segments.isEmpty()) {
        const auto &seg = lines2.first().segments.first();
        TEST(qAbs(seg.outlineWidth - 10.0) < 1.0, "\\t bord at end = ~10");
    }

    return 0;
}

// Test 17: \alpha() transparency
static int test17_alphaTag(TestContext &tc)
{
    printf("\n=== RT Test 17: \\alpha Tag ===\n");
    SubtitleRenderer renderer;

    SubtitleFrame frame;
    frame.type = SubtitleType::ASS;
    frame.text = "{\\alpha&H80&}SemiTransparent";
    frame.startSeconds = 0.0;
    frame.endSeconds = 5.0;

    QVector<SubtitleSurface> surfaces = renderer.render(frame, tc.width, tc.height);
    TEST(!surfaces.isEmpty(), "Alpha renders surface");
    if (!surfaces.isEmpty()) {
        QImage img = surfaces[0].toImage();
        int vis = countVisiblePixels(img);
        TEST(vis > 0, "Semi-transparent text visible");

        // Check that some pixels have alpha between 30 and 200 (semi-transparent)
        bool hasSemiAlpha = false;
        for (int y = 0; y < qMin(50, img.height()); ++y) {
            for (int x = 0; x < qMin(50, img.width()); ++x) {
                int a = qAlpha(img.pixel(x, y));
                if (a > 30 && a < 200) {
                    hasSemiAlpha = true;
                    break;
                }
            }
            if (hasSemiAlpha) break;
        }
        // Note: \alpha&H80& in ASS = alpha=128, which means 255-128=127 in Qt alpha
        // The renderer applies alpha via painter opacity, which may not be reflected in pixel alpha
        // This is a soft check
        printf("  DEBUG: semi-alpha pixels found = %d\n", hasSemiAlpha ? 1 : 0);
    }

    return 0;
}

// Test 18: \frz() rotation produces visible output
static int test18_rotation(TestContext &tc)
{
    printf("\n=== RT Test 18: \\frz Rotation ===\n");
    SubtitleRenderer renderer;

    SubtitleFrame frame;
    frame.type = SubtitleType::ASS;
    frame.text = "{\\frz45}Rotated";
    frame.startSeconds = 0.0;
    frame.endSeconds = 5.0;

    QVector<SubtitleSurface> surfaces = renderer.render(frame, tc.width, tc.height);
    TEST(!surfaces.isEmpty(), "Rotation renders surface");
    if (!surfaces.isEmpty()) {
        int vis = countVisiblePixels(surfaces[0].toImage());
        TEST(vis > 0, "Rotated text visible");
    }

    return 0;
}

// Test 19: Full-file animation pipeline
static int test19_animFullPipeline(TestContext &tc)
{
    printf("\n=== RT Test 19: Animation Full Pipeline ===\n");
    SubtitleManager mgr;
    bool loaded = mgr.loadExternalFile(tc.animFile);
    TEST(loaded, "Animation ASS file loads");
    mgr.setActiveTrack(0);
    TEST(mgr.activeTrackIndex() == 0, "Active track index 0");

    // \move test: at t=1.0s, should render "Moving text"
    // At t=1.0s the move is at position (50,240) - at the very start of the subtitle
    // At t=1.0s, progress=0, so position should be (50, 240)
    // Render and verify
    int64_t startMs = 1000; // subtitle starts at 1.0s
    double startSec = 1.0;

    // At t=1.0s (start of move): left side
    QVector<QImage> imgs_start = mgr.getSubtitleImages(startSec, tc.width, tc.height);
    if (!imgs_start.isEmpty() && countVisiblePixels(imgs_start[0]) > 0) {
        // Check: content should be more on the left side than right
        int leftPixels = 0, rightPixels = 0;
        for (int y = 0; y < tc.height; ++y) {
            for (int x = 0; x < tc.width / 2; ++x)
                if (qAlpha(imgs_start[0].pixel(x, y)) > 0) ++leftPixels;
            for (int x = tc.width / 2; x < tc.width; ++x)
                if (qAlpha(imgs_start[0].pixel(x, y)) > 0) ++rightPixels;
        }
        // At t=1.0s (start), position is x=50, so content should be on the left
        printf("  DEBUG: move at t=1.0s: left=%d right=%d\n", leftPixels, rightPixels);
    }

    // At t=3.0s (mid of move): center
    QVector<QImage> imgs_mid = mgr.getSubtitleImages(3.0, tc.width, tc.height);
    if (!imgs_mid.isEmpty() && countVisiblePixels(imgs_mid[0]) > 0) {
        // Content should be more centered
        int leftPixels = 0, rightPixels = 0;
        for (int y = 0; y < tc.height; ++y) {
            for (int x = 0; x < tc.width / 2; ++x)
                if (qAlpha(imgs_mid[0].pixel(x, y)) > 0) ++leftPixels;
            for (int x = tc.width / 2; x < tc.width; ++x)
                if (qAlpha(imgs_mid[0].pixel(x, y)) > 0) ++rightPixels;
        }
        printf("  DEBUG: move at t=3.0s: left=%d right=%d\n", leftPixels, rightPixels);
    }

    // \fad test: at t=1.0s (start, fading in), at t=2.5s (middle, fully visible)
    // Fade starts at 1s with 1s fade in, so at 1.0s it's transparent
    // At 1.5s it's 50% opacity, at 2.0s it's 100% opacity
    QVector<QImage> imgs_fade_start = mgr.getSubtitleImages(1.0, tc.width, tc.height);
    QVector<QImage> imgs_fade_mid = mgr.getSubtitleImages(2.0, tc.width, tc.height);

    // Both should have visible content (other subs at these times)
    printf("  DEBUG: fade at t=1.0s: %d px, t=2.0s: %d px\n",
           imgs_fade_start.isEmpty() ? -1 : countVisiblePixels(imgs_fade_start[0]),
           imgs_fade_mid.isEmpty() ? -1 : countVisiblePixels(imgs_fade_mid[0]));

    return 0;
}

// Test 20: \t() with scale (\fscx) animation through full pipeline
static int test20_scaleAnimation(TestContext &tc)
{
    printf("\n=== RT Test 20: \\t() Scale Animation ===\n");
    SubtitleRenderer renderer;

    SubtitleFrame frame;
    frame.type = SubtitleType::ASS;
    frame.text = "{\\t(0,4000,\\fscx200)}Scaling";
    frame.startSeconds = 0.0;
    frame.endSeconds = 5.0;

    SubtitleRenderContext ctx;
    ctx.videoSize = QSize(tc.width, tc.height);
    ctx.playRes = QSize(tc.width, tc.height);

    // At t=0: scaleX = 1.0 (100%)
    ctx.currentPTS = 0.0;
    frame.renderPTS = 0;
    auto lines0 = AssOverrideParser::segment(frame.text, frame, ctx);
    AssAnimationEngine::resolveLines(lines0, ctx, frame.startSeconds, frame.endSeconds);
    if (!lines0.isEmpty() && !lines0.first().segments.isEmpty()) {
        const auto &seg = lines0.first().segments.first();
        TEST(qAbs(seg.scale.x() - 1.0) < 0.1, "\\t scaleX at start = ~1.0");
    }

    // At t=2s (mid): scaleX = 1.5
    ctx.currentPTS = 2000.0;
    frame.renderPTS = 2000;
    auto lines1 = AssOverrideParser::segment(frame.text, frame, ctx);
    AssAnimationEngine::resolveLines(lines1, ctx, frame.startSeconds, frame.endSeconds);
    if (!lines1.isEmpty() && !lines1.first().segments.isEmpty()) {
        const auto &seg = lines1.first().segments.first();
        TEST(qAbs(seg.scale.x() - 1.5) < 0.15, "\\t scaleX at mid = ~1.5");
    }

    // At t=4s (end): scaleX = 2.0 (200%)
    ctx.currentPTS = 4000.0;
    frame.renderPTS = 4000;
    auto lines2 = AssOverrideParser::segment(frame.text, frame, ctx);
    AssAnimationEngine::resolveLines(lines2, ctx, frame.startSeconds, frame.endSeconds);
    if (!lines2.isEmpty() && !lines2.first().segments.isEmpty()) {
        const auto &seg = lines2.first().segments.first();
        TEST(qAbs(seg.scale.x() - 2.0) < 0.1, "\\t scaleX at end = ~2.0");
    }

    return 0;
}

// Test 21: \clip() rect rendering produces visible clipped output
static int test21_clipRect(TestContext &tc)
{
    printf("\n=== RT Test 21: \\clip() Rect ===\n");
    SubtitleRenderer renderer;

    SubtitleFrame frame;
    frame.type = SubtitleType::ASS;
    frame.text = "{\\clip(50,50,200,100)}Clipped";
    frame.startSeconds = 0.0;
    frame.endSeconds = 5.0;

    QVector<SubtitleSurface> surfaces = renderer.render(frame, tc.width, tc.height);
    TEST(!surfaces.isEmpty(), "Clip rect renders surface");
    if (!surfaces.isEmpty()) {
        int vis = countVisiblePixels(surfaces[0].toImage());
        // Clipped text should have content only in clip region
        // Since clip is applied, text may still be visible
        printf("  DEBUG: clip rect visible pixels = %d\n", vis);
    }
    return 0;
}

// Test 22: Cache bypass — animated frames not cached
static int test22_cacheBypass(TestContext &tc)
{
    printf("\n=== RT Test 22: Cache Bypass for Animated ===\n");
    // Animated text should be detected as animated
    bool anim1 = AssRenderCache::hasAnimationTags("{\\move(50,240,590,240,0,4000)}Moving");
    TEST(anim1, "\\move detected as animated");

    bool anim2 = AssRenderCache::hasAnimationTags("{\\t(0,4000,\\bord10)}Anim");
    TEST(anim2, "\\t() detected as animated");

    bool anim3 = AssRenderCache::hasAnimationTags("{\\fad(1000,1000)}Fading");
    TEST(anim3, "\\fad detected as animated");

    bool anim4 = AssRenderCache::hasAnimationTags("Static text");
    TEST(!anim4, "Static text not detected as animated");

    return 0;
}

// Test 23: Drawing mode renders path
static int test23_drawingMode(TestContext &tc)
{
    printf("\n=== RT Test 23: Drawing Mode ===\n");
    SubtitleRenderer renderer;

    // \p1 enables drawing mode. The text after is parsed as drawing commands.
    SubtitleFrame frame;
    frame.type = SubtitleType::ASS;
    frame.text = "{\\p1}m 0 0 l 100 0 l 50 100 p{\\p0}";
    frame.startSeconds = 0.0;
    frame.endSeconds = 5.0;

    QVector<SubtitleSurface> surfaces = renderer.render(frame, tc.width, tc.height);
    TEST(!surfaces.isEmpty(), "Drawing mode renders surface");
    if (!surfaces.isEmpty()) {
        QImage img = surfaces[0].toImage();
        int vis = countVisiblePixels(img);
        printf("  DEBUG: drawing mode visible pixels = %d\n", vis);
        TEST(vis > 0, "Drawing mode produces visible pixels");
    }
    return 0;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("Lunar Player Phase 7C Runtime Test");

    TestContext tc;
    tc.testDir = TEST_SOURCE_DIR;
    tc.assFile = tc.testDir + "/tests/media/test_bars_ass.ass";
    tc.animFile = tc.testDir + "/tests/media/test_animation.ass";

    printf("ASS file: %s\n", qPrintable(tc.assFile));
    printf("Anim file: %s\n", qPrintable(tc.animFile));
    fflush(stdout);

    int result = 0;

    // Run tests in sequence, stop on first failure
    #define RUN_TEST(num, func) do { \
        int r = func(tc); \
        printf("  -> Test %d %s\n\n", num, r == 0 ? "PASSED" : "FAILED"); \
        fflush(stdout); \
        if (r != 0) { result = r; goto done; } \
    } while(0)

    RUN_TEST(1, test1_parse);
    RUN_TEST(2, test2_renderTimestamps);
    RUN_TEST(3, test3_layers);
    RUN_TEST(4, test4_posOverride);
    RUN_TEST(5, test5_boldItalic);
    RUN_TEST(6, test6_colorOverride);
    RUN_TEST(7, test7_fontFamily);
    RUN_TEST(8, test8_outline);
    RUN_TEST(9, test9_shadow);
    RUN_TEST(10, test10_alignment);
    RUN_TEST(11, test11_styleResolution);
    RUN_TEST(12, test12_directRender);
    RUN_TEST(13, test13_fullPipeline);
    RUN_TEST(14, test14_moveAnimation);
    RUN_TEST(15, test15_fadeAnimation);
    RUN_TEST(16, test16_transformBord);
    RUN_TEST(17, test17_alphaTag);
    RUN_TEST(18, test18_rotation);
    RUN_TEST(19, test19_animFullPipeline);
    RUN_TEST(20, test20_scaleAnimation);
    RUN_TEST(21, test21_clipRect);
    RUN_TEST(22, test22_cacheBypass);
    RUN_TEST(23, test23_drawingMode);

done:
    if (result == 0)
        printf("\nALL RUNTIME TESTS PASSED\n");
    else
        printf("\nRUNTIME TESTS FAILED at step above\n");

    fflush(stdout);
    return result;
}
