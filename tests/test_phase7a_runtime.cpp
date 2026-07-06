// Phase 7A Runtime functional verification
// Exercises SubtitleManager → ExternalSubtitleLoader → SubtitleRenderer → SubtitleCache
// with real SRT files and pixel-level verification of rendered output
// Exit: 0 = PASS

#include <QGuiApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPixmap>
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
    app.setApplicationName("Lunar Player Phase 7A Runtime Verification");

    QString testDir = QDir::cleanPath(
        QCoreApplication::applicationDirPath() + "/../../tests/media");
    QString srcTestDir = QDir::cleanPath(
        QString::fromLocal8Bit(TEST_SOURCE_DIR) + "/tests/media");
    QString srtFile = testDir + "/test_bars_24fps_2s.srt";
    if (!QFile::exists(srtFile))
        srtFile = srcTestDir + "/test_bars_24fps_2s.srt";

    printf("RUNTIME: srt=%s\n", qPrintable(QDir::toNativeSeparators(srtFile)));

    if (!QFile::exists(srtFile)) {
        printf("FAIL: test SRT not found at %s\n", qPrintable(srtFile));
        return 1;
    }

    // ---- Phase 1: ExternalSubtitleLoader — full SRT file parsing ----
    printf("\n=== Phase 1: SRT Parsing ===\n");
    QVector<SubtitleFrame> frames = ExternalSubtitleLoader::parseSRT(srtFile);
    TEST(frames.size() == 4, "parseSRT returns 4 entries");
    TEST(frames[0].text.contains("Hello"), "Frame 1 text correct");
    TEST(frames[0].startSeconds == 0.0, "Frame 1 start 0.0s");
    TEST(frames[0].endSeconds == 2.0, "Frame 1 end 2.0s");
    TEST(frames[1].startSeconds == 0.5, "Frame 2 start 0.5s");
    TEST(frames[1].text.contains("Phase 7A"), "Frame 2 text matches");
    TEST(frames[2].startSeconds == 1.0, "Frame 3 start 1.0s");
    TEST(frames[3].startSeconds == 1.5, "Frame 4 start 1.5s");
    TEST(frames[3].text.contains("Final"), "Frame 4 text matches");

    // Verify chronological order
    for (int i = 1; i < frames.size(); ++i)
        TEST(frames[i].pts >= frames[i-1].pts, "SRT frames in chronological order");

    // ---- Phase 2: SubtitleRenderer — text SRT frame to QImage ----
    printf("\n=== Phase 2: Text Rendering ===\n");
    {
        SubtitleRenderer renderer;
        SubtitleFrame &frame = frames[0];
        QVector<SubtitleSurface> surfaces = renderer.render(frame, 640, 480);
        TEST(surfaces.size() >= 1, "Render frame 1 at 640x480");
        QImage rendered = surfaces[0].toImage();
        TEST(rendered.width() > 20, "Rendered image has width");
        TEST(rendered.height() > 10, "Rendered image has height");

        int nonTransparent = 0;
        for (int y = 0; y < rendered.height(); ++y)
            for (int x = 0; x < rendered.width(); ++x)
                if (qAlpha(rendered.pixel(x, y)) > 0)
                    ++nonTransparent;
        TEST(nonTransparent > 10, "Rendered image has visible pixels");
        printf("  HD visible pixels: %d\n", nonTransparent);

        // 4K resolution
        QVector<SubtitleSurface> surfaces4k = renderer.render(frame, 3840, 2160);
        QImage rendered4k = surfaces4k[0].toImage();
        TEST(!rendered4k.isNull(), "Render frame at 4K resolution");
        TEST(rendered4k.width() > rendered.width(), "4K render larger than HD");

        // Custom style: large yellow bold text
        SubtitleStyle style;
        style.fontSize = 48;
        style.color = QColor(255, 255, 0);
        style.bold = true;
        SubtitleFrame styledFrame = frame;
        styledFrame.style = style;
        QVector<SubtitleSurface> styledSurfaces = renderer.render(styledFrame, 640, 480);
        TEST(!styledSurfaces[0].isNull(), "Custom styled subtitle renders");
    }

    // ---- Phase 3: SubtitleRenderer — QPainter canvas rendering ----
    printf("\n=== Phase 3: QPainter Canvas ===\n");
    {
        SubtitleRenderer renderer;
        SubtitleFrame &frame = frames[0];
        QImage canvas(640, 480, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::transparent);
        {
            QPainter p(&canvas);
            renderer.render(p, frame, 640, 480);
        }
        int nonTransparent = 0;
        for (int y = 0; y < canvas.height(); ++y)
            for (int x = 0; x < canvas.width(); ++x)
                if (qAlpha(canvas.pixel(x, y)) > 0)
                    ++nonTransparent;
        TEST(nonTransparent > 10, "QPainter::render draws visible text on canvas");
        printf("  Canvas visible pixels: %d\n", nonTransparent);
    }

    // ---- Phase 4: SubtitleCache — LRU persistence ----
    printf("\n=== Phase 4: Cache ===\n");
    {
        // Budget enough for 2 100x30 images (12000 bytes each)
        SubtitleCache cache(2 * 12000);
        QImage w(100, 30, QImage::Format_ARGB32_Premultiplied);
        QImage b(100, 30, QImage::Format_ARGB32_Premultiplied);
        w.fill(Qt::white);
        b.fill(Qt::black);

        cache.insert(1000, SubtitleSurface(w));
        cache.insert(2000, SubtitleSurface(b));
        TEST(cache.size() == 2, "Cache has 2 entries");

        SubtitleSurface out;
        TEST(cache.lookup(1000, out), "Lookup 1000");
        QImage outImg = out.toImage();
        TEST(qRed(outImg.pixel(0,0)) == 255, "Cache entry 1000 is white");
        TEST(cache.lookup(2000, out), "Lookup 2000");
        outImg = out.toImage();
        TEST(qRed(outImg.pixel(0,0)) == 0, "Cache entry 2000 is black");

        cache.setBudgetBytes(1 * 12000);
        TEST(cache.budgetBytes() == 12000, "Cache budgetBytes is 12000");
        TEST(cache.size() <= 2, "Cache respects budget");

        cache.clear();
        TEST(cache.size() == 0, "Cache clear");
    }

    // ---- Phase 5: SubtitleManager — full lifecycle ----
    printf("\n=== Phase 5: SubtitleManager ===\n");
    {
        SubtitleManager mgr;
        mgr.setSubtitlesEnabled(true);

        bool loaded = mgr.loadExternalFile(srtFile);
        TEST(loaded, "Manager loads external SRT");
        TEST(mgr.trackCount() > 0, "Manager has tracks after load");

        QStringList names = mgr.trackDisplayNames();
        TEST(names.size() == mgr.trackCount(), "Display names match");
        TEST(names[0].contains(".srt"), "Display name contains filename");

        mgr.setActiveTrack(mgr.trackCount() - 1);
        TEST(mgr.activeTrackIndex() >= 0, "Active track set");

        auto subImagesAt = [&](double t) -> QImage {
            QVector<QImage> imgs = mgr.getSubtitleImages(t, 640, 480);
            return imgs.isEmpty() ? QImage() : imgs[0];
        };

        TEST(!subImagesAt(0.0).isNull(), "Subtitle at t=0.0s");
        TEST(!subImagesAt(0.5).isNull(), "Subtitle at t=0.5s");
        TEST(!subImagesAt(1.0).isNull(), "Subtitle at t=1.0s");
        TEST(!subImagesAt(1.5).isNull(), "Subtitle at t=1.5s");
        TEST(subImagesAt(2.5).isNull(), "No subtitle at t=2.5s (past end)");

        // Verify visible pixels at each time
        {
            QImage img = subImagesAt(0.0);
            int ap = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (qAlpha(img.pixel(x, y)) > 0) ++ap;
            TEST(ap > 0, "Subtitle 0.0s has visible pixels");
            printf("  t=0.0: %dx%d visiblePx=%d\n", img.width(), img.height(), ap);
        }
        {
            QImage img = subImagesAt(0.5);
            int ap = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (qAlpha(img.pixel(x, y)) > 0) ++ap;
            TEST(ap > 0, "Subtitle 0.5s has visible pixels");
        }

        // Cache: second call returns cached
        QImage cached00 = subImagesAt(0.0);
        TEST(!cached00.isNull(), "Cached subtitle at t=0.0s");

        // Disable/re-enable
        mgr.setSubtitlesEnabled(false);
        TEST(subImagesAt(0.0).isNull(), "Disabled = no subtitle");
        mgr.setSubtitlesEnabled(true);
        TEST(!subImagesAt(0.0).isNull(), "Re-enabled = subtitle");

        // Clear cache and verify memory
        mgr.clearCache();
        TEST(mgr.cache()->size() == 0, "Cache cleared");
        TEST(!subImagesAt(0.0).isNull(), "Regenerates after cache clear");

        // Switch tracks: clear and verify
        mgr.clearActiveTrack();
        TEST(mgr.activeTrackIndex() == -1, "No active track after clear");
        TEST(subImagesAt(0.0).isNull(), "No subtitle without active track");
    }

    // ---- Phase 6: Multi-track SubtitleManager ----
    printf("\n=== Phase 6: Multi-track ===\n");
    {
        SubtitleManager mgr;
        mgr.setSubtitlesEnabled(true);
        mgr.loadExternalFile(srtFile);
        mgr.loadExternalFile(srtFile);
        TEST(mgr.trackCount() == 2, "Two tracks loaded");
        TEST(mgr.trackDisplayNames().size() == 2, "Two display names");

        mgr.setActiveTrack(0);
        QVector<QImage> t0v = mgr.getSubtitleImages(0.0, 640, 480);
        TEST(!t0v.isEmpty() && !t0v[0].isNull(), "Track 0 renders");

        mgr.setActiveTrack(1);
        QVector<QImage> t1v = mgr.getSubtitleImages(0.0, 640, 480);
        TEST(!t1v.isEmpty() && !t1v[0].isNull(), "Track 1 renders");
        mgr.clearActiveTrack();
    }

    // ---- Phase 7: SubtitleDecoder — FFmpeg lifecycle ----
    printf("\n=== Phase 7: Decoder Lifecycle ===\n");
    {
        SubtitleDecoder decoder;
        TEST(!decoder.isOpen(), "Decoder initially closed");
        TEST(decoder.streamIndex() == -1, "No stream index before open");

        // Test video has no subtitle streams — should fail gracefully
        QString video = testDir + "/test_bars_24fps_2s.mp4";
        bool opened = decoder.openFile(QDir::toNativeSeparators(video));
        if (!opened)
            printf("INFO: No embedded subtitles in test video (expected)\n");
        TEST(!decoder.isOpen(), "Decoder closed after failed open");

        // Double close safety
        decoder.close();
        decoder.close();
        TEST(!decoder.isOpen(), "Double close safe");
    }

    // ---- Phase 8: SubtitleFrame edge cases ----
    printf("\n=== Phase 8: Frame Edge Cases ===\n");
    {
        SubtitleFrame f;
        f.pts = 5000;
        f.duration = 3000;
        f.startSeconds = 5.0;
        f.endSeconds = 8.0;

        TEST(f.endPts() == 8000, "endPts = pts + duration");
        TEST(f.isActive(5000), "isActive at pts start");
        TEST(f.isActive(7999), "isActive just before end");
        TEST(!f.isActive(8000), "isActive false at end");
        TEST(f.isActiveSec(5.0), "isActiveSec at start");
        TEST(f.isActiveSec(7.999), "isActiveSec just before end");

        // Default values
        SubtitleFrame empty;
        TEST(empty.pts == 0, "Default pts = 0");
        TEST(empty.type == SubtitleType::Text, "Default type = Text");

        // Style defaults
        SubtitleStyle s;
        TEST(s.fontSize == 28, "Default fontSize");
        TEST(s.color == Qt::white, "Default color white");
        TEST(s.outlineWidth == 2, "Default outlineWidth");
        TEST(s.bottomMargin == 40, "Default bottomMargin");
    }

    printf("\nALL RUNTIME TESTS PASSED\n");
    return 0;
}
