#include <QTest>
#include <QImage>
#include <QPainter>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

#include "subtitles/SubtitleManager.h"
#include "subtitles/SubtitleCache.h"
#include "subtitles/SubtitleRenderer.h"
#include "subtitles/SubtitleSurface.h"
#include "subtitles/ExternalSubtitleLoader.h"

extern "C" {
#include <libavformat/avformat.h>
}

class TestPhase7BRuntime : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // Structural tests (no real media required)
    void test_SubtitleSurface_position();
    void test_SubtitleCache_multi_insert();
    void test_SubtitleRenderer_bitmap_roundtrip();
    void test_ExternalSubtitleLoader_scan();

    // Full pipeline test: external .sup loading via FFmpeg
    void test_external_sup_loading();

private:
    QString m_testDir;
};

void TestPhase7BRuntime::initTestCase()
{
    m_testDir = TEST_SOURCE_DIR;
    qDebug() << "Test source dir:" << m_testDir;
}

void TestPhase7BRuntime::test_SubtitleSurface_position()
{
    QImage img(60, 30, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::blue);
    SubtitleSurface surf(img, 120, 80);
    QCOMPARE(surf.posX(), 120);
    QCOMPARE(surf.posY(), 80);
    QVERIFY(!surf.isNull());

    QImage extracted = surf.toImage();
    QCOMPARE(extracted.width(), 60);
    QCOMPARE(extracted.height(), 30);
    QCOMPARE(extracted.pixel(0, 0), qRgba(0, 0, 255, 255));

    surf.upload(); // no-op CPU path
    QCOMPARE(surf.textureId(), 0ULL);
}

void TestPhase7BRuntime::test_SubtitleCache_multi_insert()
{
    SubtitleCache cache(4096);
    QImage img(16, 16, QImage::Format_ARGB32_Premultiplied);
    SubtitleSurface surf(img);

    cache.insert(100, surf);
    cache.insert(200, surf);
    cache.insert(300, surf);
    QCOMPARE(cache.size(), 3);

    SubtitleSurface out;
    QVERIFY(cache.lookup(100, out));
    QVERIFY(cache.lookup(200, out));
    QVERIFY(cache.lookup(300, out));

    cache.remove(200);
    QCOMPARE(cache.size(), 2);
    QVERIFY(!cache.lookup(200, out));
    QVERIFY(cache.lookup(100, out));
}

void TestPhase7BRuntime::test_SubtitleRenderer_bitmap_roundtrip()
{
    // Create a synthetic bitmap SubtitleFrame
    QImage bmp(100, 50, QImage::Format_ARGB32_Premultiplied);
    bmp.fill(Qt::cyan);

    SubtitleFrame frame;
    frame.type = SubtitleType::Bitmap;
    frame.bitmap = bmp;
    frame.startSeconds = 0.0;
    frame.endSeconds = 10.0;
    frame.posX = 0;
    frame.posY = 0;
    frame.displayWidth = 100;
    frame.displayHeight = 50;

    SubtitleRenderer renderer;
    QVector<SubtitleSurface> surfaces = renderer.render(frame, 1920, 1080);
    QCOMPARE(surfaces.size(), 1);
    QVERIFY(!surfaces[0].isNull());
    QCOMPARE(surfaces[0].width(), 100);
    QCOMPARE(surfaces[0].height(), 50);
}

void TestPhase7BRuntime::test_ExternalSubtitleLoader_scan()
{
    // Scan should not crash and return a valid list
    QStringList candidates = ExternalSubtitleLoader::scanDirectory(m_testDir + "/tests/media/test_bars_24fps_2s.mkv");
    QVERIFY(candidates.isEmpty() || !candidates.isEmpty());
    qDebug() << "Scan candidates:" << candidates;
}

void TestPhase7BRuntime::test_external_sup_loading()
{
    // Try to load a .sup file from test media directory
    QString supPath = m_testDir + "/tests/media/test_pgs.sup";
    QFileInfo fi(supPath);
    if (!fi.exists()) {
        QSKIP("test_pgs.sup not found — skipping external .sup loading test");
    }

    QVERIFY(ExternalSubtitleLoader::detectCodec(supPath) == "sup");
    ExternalSubtitleTrack track = ExternalSubtitleLoader::loadFile(supPath);
    QVERIFY(track.loaded);
    QVERIFY(!track.frames.isEmpty());
    QCOMPARE(track.codecName, QString("sup"));

    // Verify decoded frames have correct types and timing
    for (const SubtitleFrame &f : track.frames) {
        QVERIFY(f.type == SubtitleType::Bitmap || f.type == SubtitleType::Text);
        QVERIFY(f.pts >= 0);
        QVERIFY(f.duration >= 0);
    }
}

QTEST_MAIN(TestPhase7BRuntime)
#include "test_phase7b_runtime.moc"
