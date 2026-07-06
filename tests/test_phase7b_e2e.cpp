// Phase 7B End-to-End functional verification
// Exercises the full pipeline:
//   ExternalSubtitleLoader → SubtitleManager → SubtitleRenderer → SubtitleSurface → SubtitleCache → QImage
// with real SRT files, cache budget enforcement, and multi-image rendering
// Exit: 0 = PASS

#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <cstdio>
#include "subtitles/SubtitleFrame.h"
#include "subtitles/SubtitleSurface.h"
#include "subtitles/SubtitleCache.h"
#include "subtitles/SubtitleRenderer.h"
#include "subtitles/ExternalSubtitleLoader.h"
#include "subtitles/SubtitleManager.h"
#include "subtitles/SubtitleDecoder.h"

#define TEST(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fflush(stdout); return 1; } \
    printf("PASS: %s\n", msg); fflush(stdout); \
} while(0)

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("Lunar Player Phase 7B E2E Verification");

    QString testDir = QDir::cleanPath(
        QCoreApplication::applicationDirPath() + "/../../tests/media");
    QString srcTestDir = QDir::cleanPath(
        QString::fromLocal8Bit(TEST_SOURCE_DIR) + "/tests/media");
    QString srtFile = testDir + "/test_bars_24fps_2s.srt";
    if (!QFile::exists(srtFile))
        srtFile = srcTestDir + "/test_bars_24fps_2s.srt";

    printf("E2E: srt=%s\n", qPrintable(QDir::toNativeSeparators(srtFile)));

    if (!QFile::exists(srtFile)) {
        printf("FAIL: test SRT not found at %s\n", qPrintable(srtFile));
        return 1;
    }

    // ---- Test 1: Full pipeline with SubtitleManager ----
    printf("\n=== E2E Test 1: SubtitleManager full pipeline ===\n");
    {
        SubtitleManager mgr;
        mgr.setSubtitlesEnabled(true);

        // Load external SRT
        bool loaded = mgr.loadExternalFile(srtFile);
        TEST(loaded, "Manager loads external SRT");
        TEST(mgr.trackCount() == 1, "One track after load");

        // Activate track
        mgr.setActiveTrack(0);
        TEST(mgr.activeTrackIndex() == 0, "Active track index 0");

        // Get subtitle images at various timestamps
        int subCheckResult = 0;
        auto checkSubs = [&](double t, bool expectVisible) {
            QVector<QImage> imgs = mgr.getSubtitleImages(t, 640, 480);
            if (expectVisible) {
                if (imgs.isEmpty()) { printf("FAIL: Sub at t=%1 exists\n", t); fflush(stdout); subCheckResult = 1; return; }
                int alpha = 0;
                for (int y = 0; y < imgs[0].height(); ++y)
                    for (int x = 0; x < imgs[0].width(); ++x)
                        if (qAlpha(imgs[0].pixel(x, y)) > 0) ++alpha;
                if (alpha == 0) { printf("FAIL: Sub at t=%1 visible\n", t); fflush(stdout); subCheckResult = 1; return; }
                printf("PASS: Sub at t=%1 visible\n", t); fflush(stdout);
            } else {
                if (!imgs.isEmpty()) { printf("FAIL: No sub at t=%1\n", t); fflush(stdout); subCheckResult = 1; return; }
                printf("PASS: No sub at t=%1\n", t); fflush(stdout);
            }
        };

        checkSubs(0.0, true);
        checkSubs(0.5, true);
        checkSubs(1.0, true);
        checkSubs(1.5, true);
        checkSubs(2.5, false);
        checkSubs(3.0, false);
        if (subCheckResult) return subCheckResult;
    }

    // ---- Test 2: Cache budget enforcement ----
    printf("\n=== E2E Test 2: Cache budget enforcement ===\n");
    {
        SubtitleCache cache(1024); // 1KB budget
        QImage big(64, 64, QImage::Format_ARGB32_Premultiplied); // 16384 bytes each

        cache.insert(1, SubtitleSurface(big));
        // Single image exceeds budget, but we allow it (no eviction if nothing to evict)
        TEST(cache.size() == 1, "Single oversized entry allowed");

        // Insert second - should evict first
        cache.insert(2, SubtitleSurface(big));
        SubtitleSurface out;
        bool found1 = cache.lookup(1, out);
        bool found2 = cache.lookup(2, out);
        TEST(!found1 && found2, "LRU evicted first entry");
        TEST(cache.usedBytes() <= cache.budgetBytes() + big.width() * big.height() * 4,
             "Budget enforced after eviction");
    }

    // ---- Test 3: Multi-image rendering (QVector<SubtitleSurface>) ----
    printf("\n=== E2E Test 3: Multi-image rendering ===\n");
    {
        SubtitleRenderer renderer;

        // Render text at two different timestamps
        SubtitleFrame frame1, frame2;
        frame1.type = SubtitleType::Text;
        frame1.text = "Hello";
        frame2.type = SubtitleType::Text;
        frame2.text = "World";

        QVector<SubtitleSurface> r1 = renderer.render(frame1, 640, 480);
        QVector<SubtitleSurface> r2 = renderer.render(frame2, 640, 480);

        TEST(r1.size() == 1, "Text produces one surface");
        TEST(r2.size() == 1, "Second text produces one surface");

        QImage img1 = r1[0].toImage();
        QImage img2 = r2[0].toImage();

        TEST(!img1.isNull(), "Surface toImage non-null");
        TEST(!img2.isNull(), "Second surface toImage non-null");
        TEST(img1.width() > 0 && img1.height() > 0, "Surface has dimensions");
    }

    // ---- Test 4: QPainter render compositing ----
    printf("\n=== E2E Test 4: QPainter compositing ===\n");
    {
        SubtitleRenderer renderer;
        SubtitleFrame frame;
        frame.type = SubtitleType::Text;
        frame.text = "Composite Test";

        QImage canvas(640, 480, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::transparent);
        {
            QPainter p(&canvas);
            renderer.render(p, frame, 640, 480);
        }
        int visible = 0;
        for (int y = 0; y < canvas.height(); ++y)
            for (int x = 0; x < canvas.width(); ++x)
                if (qAlpha(canvas.pixel(x, y)) > 0) ++visible;
        TEST(visible > 0, "QPainter compositing produces visible output");
        printf("  Visible pixels: %d\n", visible);
    }

    // ---- Test 5: SubtitleManager with cache budget configuration ----
    printf("\n=== E2E Test 5: Budget configuration ===\n");
    {
        SubtitleManager mgr;
        TEST(mgr.cacheBudgetBytes() == 128LL * 1024 * 1024, "Default budget 128MB");

        mgr.setCacheBudgetMB(64);
        TEST(mgr.cacheBudgetBytes() == 64LL * 1024 * 1024, "Budget set to 64MB");

        mgr.setCacheBudgetMB(512);
        TEST(mgr.cacheBudgetBytes() == 512LL * 1024 * 1024, "Budget set to 512MB");
    }

    // ---- Test 6: SubtitleFrame cacheKey uniqueness ----
    printf("\n=== E2E Test 6: cacheKey uniqueness ===\n");
    {
        SubtitleFrame a, b, c;
        a.trackIndex = 0; a.subtitleIndex = 0; a.pts = 1000;
        b.trackIndex = 0; b.subtitleIndex = 1; b.pts = 2000;
        c.trackIndex = 1; c.subtitleIndex = 0; c.pts = 1000;

        uint64_t ka = a.cacheKey();
        uint64_t kb = b.cacheKey();
        uint64_t kc = c.cacheKey();
        TEST(ka != kb, "Different subIndex → different key");
        TEST(ka != kc, "Different trackIndex → different key");
        TEST(kb != kc, "Different both → different key");

        // Same inputs → same key
        SubtitleFrame a2;
        a2.trackIndex = 0; a2.subtitleIndex = 0; a2.pts = 1000;
        TEST(a.cacheKey() == a2.cacheKey(), "Same inputs → same key");
    }

    // ---- Test 7: ExternalSubtitleLoader format detection for bitmap types ----
    printf("\n=== E2E Test 7: Bitmap format detection ===\n");
    {
        TEST(ExternalSubtitleLoader::detectCodec("test.sup") == "sup", ".sup detected");
        TEST(ExternalSubtitleLoader::detectCodec("test.idx") == "vobsub", ".idx detected");
        TEST(ExternalSubtitleLoader::detectCodec("test.sub") == "vobsub", ".sub detected");
        TEST(ExternalSubtitleLoader::detectCodec("test.srt") == "srt", ".srt still works");
        TEST(ExternalSubtitleLoader::detectCodec("test.ass") == "ass", ".ass still works");
        TEST(ExternalSubtitleLoader::detectCodec("test.vtt") == "vtt", ".vtt still works");
    }

    printf("\nALL E2E TESTS PASSED\n");
    return 0;
}
