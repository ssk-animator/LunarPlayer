// Phase 7A: Subtitle infrastructure verification (updated for Phase 7B API)
// Exit: 0 = PASS

#include <QApplication>
#include <QDir>
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
    app.setApplicationName("Lunar Player Phase 7A Verification");

    // ---- SubtitleFrame basics ----
    {
        SubtitleFrame frame;
        frame.pts = 1000;
        frame.duration = 2000;
        frame.startSeconds = 1.0;
        frame.endSeconds = 3.0;
        TEST(frame.isActive(1000), "SubtitleFrame::isActive at start PTS");
        TEST(frame.isActive(2500), "SubtitleFrame::isActive mid PTS");
        TEST(!frame.isActive(3000), "SubtitleFrame::isActive at end PTS");
        TEST(!frame.isActive(999), "SubtitleFrame::isActive before start");
        TEST(frame.isActiveSec(1.0), "SubtitleFrame::isActiveSec at start");
        TEST(frame.isActiveSec(2.5), "SubtitleFrame::isActiveSec mid");
        TEST(!frame.isActiveSec(3.0), "SubtitleFrame::isActiveSec at end");
        TEST(frame.endPts() == 3000, "SubtitleFrame::endPts");
    }

    // ---- SubtitleCache ----
    {
        // Budget enough for 4 small 10x10 images (400 bytes each)
        SubtitleCache cache(4 * 400);
        QImage img1(10, 10, QImage::Format_ARGB32);
        QImage img2(10, 10, QImage::Format_ARGB32);
        img1.fill(Qt::red);
        img2.fill(Qt::green);

        cache.insert(1000, SubtitleSurface(img1));
        cache.insert(2000, SubtitleSurface(img2));

        SubtitleSurface out;
        TEST(cache.lookup(1000, out), "SubtitleCache lookup pt1");
        QImage outImg = out.toImage();
        TEST(qRed(outImg.pixel(0, 0)) == 255 && qGreen(outImg.pixel(0, 0)) == 0 && qBlue(outImg.pixel(0, 0)) == 0,
             "SubtitleCache pt1 content");
        TEST(cache.lookup(2000, out), "SubtitleCache lookup pt2");

        // Eviction: insert 3 more to exceed budget
        cache.insert(3000, SubtitleSurface(QImage(10, 10, QImage::Format_ARGB32)));
        cache.insert(4000, SubtitleSurface(QImage(10, 10, QImage::Format_ARGB32)));
        cache.insert(5000, SubtitleSurface(QImage(10, 10, QImage::Format_ARGB32)));
        // pt1 (1000) should be evicted (least recently used)
        TEST(!cache.lookup(1000, out), "SubtitleCache eviction LRU");
        TEST(cache.lookup(2000, out), "SubtitleCache pt2 survives");

        cache.clear();
        TEST(cache.size() == 0, "SubtitleCache clear");
    }

    // ---- SubtitleRenderer text ----
    {
        SubtitleRenderer renderer;
        SubtitleFrame textFrame;
        textFrame.type = SubtitleType::Text;
        textFrame.text = "Hello Subtitles";
        textFrame.startSeconds = 0.0;
        textFrame.endSeconds = 5.0;

        QVector<SubtitleSurface> surfaces = renderer.render(textFrame, 640, 480);
        TEST(surfaces.size() >= 1, "SubtitleRenderer text produces surfaces");
        TEST(!surfaces[0].isNull(), "SubtitleRenderer text surface non-null");
        TEST(surfaces[0].width() > 0 && surfaces[0].height() > 0, "SubtitleRenderer non-empty");

        // Render with a QPainter directly
        QImage canvas(640, 480, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::transparent);
        QPainter p(&canvas);
        renderer.render(p, textFrame, 640, 480);
        p.end();
        // Should have drawn something non-transparent at bottom
        bool hasContent = false;
        for (int y = 0; y < canvas.height(); ++y) {
            for (int x = 0; x < canvas.width(); ++x) {
                if (qAlpha(canvas.pixel(x, y)) > 0) { hasContent = true; break; }
            }
            if (hasContent) break;
        }
        TEST(hasContent, "SubtitleRenderer::render draws visible text");
    }

    // ---- SubtitleRenderer bitmap ----
    {
        SubtitleRenderer renderer;
        QImage bitmap(64, 32, QImage::Format_ARGB32);
        bitmap.fill(Qt::red);
        SubtitleFrame bitmapFrame;
        bitmapFrame.type = SubtitleType::Bitmap;
        bitmapFrame.bitmap = bitmap;

        QVector<SubtitleSurface> surfaces = renderer.render(bitmapFrame, 640, 480);
        TEST(surfaces.size() >= 1, "SubtitleRenderer bitmap produces surfaces");
        QImage rendered = surfaces[0].toImage();
        TEST(rendered.width() == 64 && rendered.height() == 32,
             "SubtitleRenderer bitmap preserves size");
    TEST(qRed(rendered.pixel(0, 0)) == 255 && qGreen(rendered.pixel(0, 0)) == 0 && qBlue(rendered.pixel(0, 0)) == 0,
         "SubtitleRenderer bitmap pixel content");
    }

    // ---- ExternalSubtitleLoader SRT parsing ----
    {
        QString testDir = QDir::cleanPath(
            QApplication::applicationDirPath() + "/../../tests/media");
        QString srcTestDir = QDir::cleanPath(
            QString::fromLocal8Bit(TEST_SOURCE_DIR) + "/tests/media");
        QString srtFile = testDir + "/test_bars_24fps_2s.srt";
        if (!QFile::exists(srtFile))
            srtFile = srcTestDir + "/test_bars_24fps_2s.srt";

        if (QFile::exists(srtFile)) {
            QVector<SubtitleFrame> frames = ExternalSubtitleLoader::parseSRT(srtFile);
            TEST(frames.size() > 0, "ExternalSubtitleLoader parseSRT returns frames");
            if (!frames.isEmpty()) {
                TEST(!frames[0].text.isEmpty(), "ExternalSubtitleLoader SRT text non-empty");
                TEST(frames[0].startSeconds < frames[0].endSeconds,
                     "ExternalSubtitleLoader SRT valid time range");
                TEST(frames[0].pts >= 0, "ExternalSubtitleLoader SRT pts set");
            }
        } else {
            printf("SKIP: SRT test file not found at %s\n", qPrintable(srtFile));
        }
    }

    // ---- ExternalSubtitleLoader codec detection ----
    {
        TEST(ExternalSubtitleLoader::detectCodec("test.srt") == "srt",
             "detectCodec .srt");
        TEST(ExternalSubtitleLoader::detectCodec("test.ass") == "ass",
             "detectCodec .ass");
        TEST(ExternalSubtitleLoader::detectCodec("test.ssa") == "ass",
             "detectCodec .ssa");
        TEST(ExternalSubtitleLoader::detectCodec("test.vtt") == "vtt",
             "detectCodec .vtt");
    }

    // ---- ExternalSubtitleLoader language extraction ----
    {
        TEST(ExternalSubtitleLoader::extractLanguage("Movie.en.srt") == "en",
             "extractLanguage en");
        TEST(ExternalSubtitleLoader::extractLanguage("Movie.eng.srt") == "eng",
             "extractLanguage eng");
        TEST(ExternalSubtitleLoader::extractLanguage("Movie.french.srt") == "french",
             "extractLanguage french");
        TEST(ExternalSubtitleLoader::extractLanguage("Movie.srt").isEmpty(),
             "extractLanguage none");
    }

    // ---- SubtitleManager lifecycle ----
    {
        SubtitleManager manager;
        TEST(manager.trackCount() == 0, "SubtitleManager initial empty");
    TEST(manager.subtitlesEnabled(), "SubtitleManager starts enabled");

    manager.setSubtitlesEnabled(false);
    TEST(!manager.subtitlesEnabled(), "SubtitleManager disable");

    // No active track -> no images
    QVector<QImage> imgs = manager.getSubtitleImages(0.0, 640, 480);
    TEST(imgs.isEmpty(), "SubtitleManager no image without active track");

    // Disabled -> no images even with track
    manager.setSubtitlesEnabled(true);
    manager.setSubtitlesEnabled(false);
    imgs = manager.getSubtitleImages(0.0, 640, 480);
    TEST(imgs.isEmpty(), "SubtitleManager no image when disabled");
    }

    // ---- SubtitleFrame default style ----
    {
        SubtitleStyle style;
        TEST(style.fontFamily == "Arial", "SubtitleStyle default font");
        TEST(style.fontSize == 28, "SubtitleStyle default size");
        TEST(style.color == Qt::white, "SubtitleStyle default color");
        TEST(style.bottomMargin == 40, "SubtitleStyle default bottom margin");
    }

    // ---- SubtitleRenderer style override ----
    {
        SubtitleRenderer renderer;
        SubtitleStyle customStyle;
        customStyle.fontSize = 48;
        customStyle.color = QColor(255, 255, 0);
        renderer.setDefaultStyle(customStyle);

        SubtitleFrame frame;
        frame.type = SubtitleType::Text;
        frame.text = "Style Test";
        frame.style = customStyle;

        QVector<SubtitleSurface> surfaces = renderer.render(frame, 640, 480);
        TEST(!surfaces.isEmpty() && !surfaces[0].isNull(), "SubtitleRenderer custom style produces surface");
    }

    // ---- SubtitleCache budget ----
    {
        // Budget: enough for 4 1x1 images
        SubtitleCache cache(4 * 4);
        TEST(cache.budgetBytes() == 16, "SubtitleCache initial budgetBytes");
        cache.setBudgetBytes(3 * 4);
        TEST(cache.budgetBytes() == 12, "SubtitleCache updated budgetBytes");
        cache.insert(1, SubtitleSurface(QImage(1, 1, QImage::Format_ARGB32_Premultiplied)));
        cache.insert(2, SubtitleSurface(QImage(1, 1, QImage::Format_ARGB32_Premultiplied)));
        cache.insert(3, SubtitleSurface(QImage(1, 1, QImage::Format_ARGB32_Premultiplied)));
        cache.insert(4, SubtitleSurface(QImage(1, 1, QImage::Format_ARGB32_Premultiplied))); // should evict 1
        SubtitleSurface out;
        TEST(!cache.lookup(1, out), "SubtitleCache budget enforced");
        TEST(cache.lookup(2, out), "SubtitleCache item 2 survives");
    }

    printf("ALL TESTS PASSED\n");
    return 0;
}
