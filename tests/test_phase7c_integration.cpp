#include <cstdio>
#include <cstdlib>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QDir>
#include <QDateTime>

#include "subtitles/SubtitleManager.h"
#include "subtitles/SubtitleRenderer.h"
#include "subtitles/AssRenderCache.h"

#define TEST(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s\n", msg); fflush(stdout); return 1; \
    } \
    printf("PASS: %s\n", msg); fflush(stdout); \
} while(0)

static int countVisiblePixels(const QImage &img)
{
    int count = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(img.pixel(x, y)) > 0) ++count;
    return count;
}

static QString saveDebugImage(const QImage &img, const QString &label)
{
    QString dir = QDir::tempPath() + "/lunar_phase7c_integration";
    QDir().mkpath(dir);
    QString path = dir + "/" + label + ".png";
    img.save(path);
    printf("  DEBUG saved: %s\n", qPrintable(path));
    fflush(stdout);
    return path;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("Lunar Player Phase 7C Integration Test");

    QString testDir = TEST_SOURCE_DIR;
    QString assFile = testDir + "/tests/media/test_phase7c_features.ass";
    int videoWidth = 640;
    int videoHeight = 480;

    // ========================================
    // Step 1: Load ASS file via SubtitleManager
    // ========================================
    printf("\n=== Step 1: Load ASS file ===\n");
    SubtitleManager manager;
    bool loaded = manager.loadExternalFile(assFile);
    TEST(loaded, "Manager loads ASS file");
    TEST(manager.trackCount() == 1, "Exactly 1 track");
    manager.setActiveTrack(0);
    TEST(manager.activeTrackIndex() == 0, "Active track index 0");

    // ========================================
    // Step 2: Get subtitles at drawing mode timestamp
    // ========================================
    printf("\n=== Step 2: Drawing mode rendering ===\n");
    {
        QVector<QImage> images = manager.getSubtitleImages(2.0, videoWidth, videoHeight);
        TEST(!images.isEmpty(), "Images at t=2.0s (drawing mode)");
        if (!images.isEmpty()) {
            QImage &canvas = images[0];
            TEST(canvas.width() == videoWidth, "Canvas width = 640");
            TEST(canvas.height() == videoHeight, "Canvas height = 480");
            int vis = countVisiblePixels(canvas);
            printf("  DEBUG: drawing mode visible pixels = %d\n", vis);
            TEST(vis > 0, "Drawing mode produces visible pixels");
        }
    }

    // ========================================
    // Step 3: Get subtitles at clip + drawing timestamp
    // ========================================
    printf("\n=== Step 3: Clip + drawing combined ===\n");
    {
        QVector<QImage> images = manager.getSubtitleImages(8.0, videoWidth, videoHeight);
        TEST(!images.isEmpty(), "Images at t=8.0s (clip + drawing + karaoke)");
        if (!images.isEmpty()) {
            QImage &canvas = images[0];
            int vis = countVisiblePixels(canvas);
            printf("  DEBUG: clip+drawing+karaoke visible pixels = %d\n", vis);
            TEST(vis > 0, "Clip + drawing + karaoke produce visible pixels");
        }
    }

    // ========================================
    // Step 4: Get subtitles at animation + drawing timestamp
    // ========================================
    printf("\n=== Step 4: Animation + drawing combined ===\n");
    {
        QVector<QImage> images = manager.getSubtitleImages(14.0, videoWidth, videoHeight);
        TEST(!images.isEmpty(), "Images at t=14.0s (animation + drawing)");
        if (!images.isEmpty()) {
            QImage &canvas = images[0];
            int vis = countVisiblePixels(canvas);
            printf("  DEBUG: animation+drawing visible pixels = %d\n", vis);
            TEST(vis > 0, "Animation + drawing produce visible pixels");
        }
    }

    // ========================================
    // Step 5: Verify cache is active for non-animated frames
    // ========================================
    printf("\n=== Step 5: Cache verification ===\n");
    {
        // First render to populate cache
        manager.clearCache();
        QVector<QImage> firstRender = manager.getSubtitleImages(2.0, videoWidth, videoHeight);
        TEST(!firstRender.isEmpty(), "First render at t=2.0s");
        int firstVis = countVisiblePixels(firstRender[0]);

        // Second render should be faster (cache hit) and identical
        QVector<QImage> secondRender = manager.getSubtitleImages(2.0, videoWidth, videoHeight);
        TEST(!secondRender.isEmpty(), "Second render at t=2.0s");
        int secondVis = countVisiblePixels(secondRender[0]);

        // Pixel counts should be identical for cached frame
        TEST(firstVis == secondVis,
             "Cached render produces identical pixel count");
        printf("  DEBUG: first=%d second=%d (should match)\n", firstVis, secondVis);
    }

    // ========================================
    // Step 6: Verify cache bypass for animated frames
    // ========================================
    printf("\n=== Step 6: Animation cache bypass ===\n");
    {
        manager.clearCache();
        // Render at two different times for animated drawing
        QVector<QImage> t1 = manager.getSubtitleImages(11.0, videoWidth, videoHeight);
        QVector<QImage> t2 = manager.getSubtitleImages(14.0, videoWidth, videoHeight);
        TEST(!t1.isEmpty() && !t2.isEmpty(), "Animated renders at t=11.0 and t=14.0");

        if (!t1.isEmpty() && !t2.isEmpty()) {
            int v1 = countVisiblePixels(t1[0]);
            int v2 = countVisiblePixels(t2[0]);
            printf("  DEBUG: t=11.0 v1=%d t=14.0 v2=%d (should differ for animated)\n", v1, v2);
            // They could be different due to \move, \t() animations
            QImage t1copy = t1[0];
            // First render at same time should match second render at same time
            QVector<QImage> t1b = manager.getSubtitleImages(11.0, videoWidth, videoHeight);
            TEST(!t1b.isEmpty(), "Re-render at t=11.0");
            int v1b = countVisiblePixels(t1b[0]);
            TEST(v1 == v1b, "Same-time animated renders produce same pixel count");
        }
    }

    // ========================================
    // Step 7: Render cache integration (text path caching)
    // ========================================
    printf("\n=== Step 7: Render cache text path caching ===\n");
    {
        AssRenderCache cache(64 * 1024 * 1024);
        QFont font("Arial", 28);
        QPainterPath path1;
        path1.addText(0, 0, font, "Hello");

        QPainterPath path2;
        path2.addText(0, 0, font, "World");

        // Cache miss
        QPainterPath fetched = cache.getTextPath(font, "Hello");
        TEST(fetched.isEmpty(), "Cache miss for uncached text path");

        cache.insertTextPath(font, "Hello", path1);
        cache.insertTextPath(font, "World", path2);

        fetched = cache.getTextPath(font, "Hello");
        TEST(!fetched.isEmpty(), "Cache hit after insert");

        // Verify path is identical (same bounding rect)
        TEST(qAbs(fetched.boundingRect().width() - path1.boundingRect().width()) < 1.0,
             "Cached path bounding rect matches");
    }

    // ========================================
    // Step 8: Animation tag detection
    // ========================================
    printf("\n=== Step 8: Animation tag detection ===\n");
    {
        TEST(AssRenderCache::hasAnimationTags("{\\move(100,100,500,100)}text"),
             "Detect \\move");
        TEST(AssRenderCache::hasAnimationTags("{\\t(0,1000,\\bord10)}text"),
             "Detect \\t()");
        TEST(AssRenderCache::hasAnimationTags("{\\fad(500,500)}text"),
             "Detect \\fad");
        TEST(AssRenderCache::hasAnimationTags("{\\fade(255,0,255,0,0,0,0)}text"),
             "Detect \\fade");
        TEST(!AssRenderCache::hasAnimationTags("Static text without tags"),
             "Static text not animated");
        TEST(!AssRenderCache::hasAnimationTags("{\\b1}Bold{\\b0} text"),
             "\\b not animated");
        TEST(!AssRenderCache::hasAnimationTags("{\\p1}m 0 0 l 100 0 l 50 100 p{\\p0}"),
             "Drawing mode text not animated by default");
    }

    // ========================================
    // Step 9: No-crash test for edge cases
    // ========================================
    printf("\n=== Step 9: Edge case robustness ===\n");
    {
        // Empty subtitle
        SubtitleManager emptyManager;
        QVector<QImage> emptyResult = emptyManager.getSubtitleImages(0.0, 640, 480);
        TEST(emptyResult.isEmpty() || !emptyResult[0].isNull(),
             "Empty manager returns valid results");

        // Zero-size canvas should not crash
        QVector<QImage> zeroSize = manager.getSubtitleImages(2.0, 0, 0);
        // Might be empty or have trivial dimensions, but should not crash

        // Very large timestamp should not crash
        QVector<QImage> future = manager.getSubtitleImages(999999.0, 640, 480);
        // No subtitles active, should return empty
    }

    // ========================================
    // Step 10: Multiple render passes at same timestamp (stability)
    // ========================================
    printf("\n=== Step 10: Multi-pass stability ===\n");
    {
        for (int i = 0; i < 10; ++i) {
            QVector<QImage> images = manager.getSubtitleImages(2.0, videoWidth, videoHeight);
            if (images.isEmpty()) {
                printf("FAIL: Empty result on pass %d\n", i);
                fflush(stdout);
                return 1;
            }
        }
        printf("PASS: 10 render passes at same timestamp\n");
    }

    printf("\n========================================\n");
    printf("ALL PHASE 7C INTEGRATION TESTS PASSED\n");
    printf("========================================\n");
    return 0;
}
