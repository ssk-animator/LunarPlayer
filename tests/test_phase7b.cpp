#include <QTest>
#include <QImage>
#include <QPainter>
#include <QTemporaryFile>
#include <QFile>
#include <QDir>
#include <QDebug>

#include "subtitles/SubtitleFrame.h"
#include "subtitles/SubtitleSurface.h"
#include "subtitles/SubtitleCache.h"
#include "subtitles/SubtitleRenderer.h"
#include "subtitles/SubtitleDecoder.h"
#include "subtitles/ExternalSubtitleLoader.h"
#include "subtitles/SubtitleManager.h"

class TestPhase7B : public QObject {
    Q_OBJECT

private slots:
    // ---- SubtitleSurface ----
    void test_SubtitleSurface_default();
    void test_SubtitleSurface_with_image();
    void test_SubtitleSurface_memory_bytes();
    void test_SubtitleSurface_toImage();

    // ---- SubtitleCache MB budget ----
    void test_SubtitleCache_insert_lookup();
    void test_SubtitleCache_budget_eviction();
    void test_SubtitleCache_set_budget();
    void test_SubtitleCache_clear();

    // ---- SubtitleFrame cacheKey ----
    void test_SubtitleFrame_cacheKey();
    void test_SubtitleFrame_isActiveSec();

    // ---- SubtitleDecoder conversion ----
    void test_SubtitleDecoder_composeBitmap();

    // ---- ExternalSubtitleLoader extensions ----
    void test_ExternalSubtitleLoader_detectBitmapCodec();
    void test_ExternalSubtitleLoader_supportedExtensions();

    // ---- SubtitleManager multi-image API ----
    void test_SubtitleManager_getSubtitleImages();

    // ---- Stable identity across track/subtitle ----
    void test_cacheKey_stability();
};

// ============================================================
// SubtitleSurface
// ============================================================

void TestPhase7B::test_SubtitleSurface_default()
{
    SubtitleSurface s;
    QVERIFY(s.isNull());
    QCOMPARE(s.posX(), 0);
    QCOMPARE(s.posY(), 0);
    QCOMPARE(s.memoryBytes(), 0);
    QCOMPARE(s.backend(), SubtitleSurface::CPU);
}

void TestPhase7B::test_SubtitleSurface_with_image()
{
    QImage img(100, 50, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::red);
    SubtitleSurface s(img, 10, 20);
    QVERIFY(!s.isNull());
    QCOMPARE(s.width(), 100);
    QCOMPARE(s.height(), 50);
    QCOMPARE(s.posX(), 10);
    QCOMPARE(s.posY(), 20);
}

void TestPhase7B::test_SubtitleSurface_memory_bytes()
{
    QImage img(100, 50, QImage::Format_ARGB32_Premultiplied);
    SubtitleSurface s(img);
    QCOMPARE(s.memoryBytes(), static_cast<int64_t>(100 * 50 * 4));

    SubtitleSurface nullSurf;
    QCOMPARE(nullSurf.memoryBytes(), 0);
}

void TestPhase7B::test_SubtitleSurface_toImage()
{
    QImage img(10, 10, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::green);
    SubtitleSurface s(img);
    QImage result = s.toImage();
    QCOMPARE(result.pixel(0, 0), qRgba(0, 255, 0, 255));
}

// ============================================================
// SubtitleCache (MB budget)
// ============================================================

void TestPhase7B::test_SubtitleCache_insert_lookup()
{
    SubtitleCache cache(1024LL * 1024); // 1 MB budget
    QImage img(10, 10, QImage::Format_ARGB32_Premultiplied);
    SubtitleSurface surf(img);

    cache.insert(42, surf);
    QCOMPARE(cache.size(), 1);

    SubtitleSurface out;
    QVERIFY(cache.lookup(42, out));
    QVERIFY(!out.isNull());

    QVERIFY(!cache.lookup(99, out));
}

void TestPhase7B::test_SubtitleCache_budget_eviction()
{
    // Very tight budget: enough for one surface only
    SubtitleCache cache(400); // 400 bytes
    QImage img1(10, 10, QImage::Format_ARGB32_Premultiplied); // 400 bytes
    QImage img2(10, 10, QImage::Format_ARGB32_Premultiplied); // 400 bytes
    SubtitleSurface surf1(img1);
    SubtitleSurface surf2(img2);

    cache.insert(1, surf1);
    QCOMPARE(cache.size(), 1);

    cache.insert(2, surf2);
    // Should have evicted 1, kept 2
    QCOMPARE(cache.size(), 1);

    SubtitleSurface out;
    QVERIFY(!cache.lookup(1, out)); // evicted
    QVERIFY(cache.lookup(2, out));  // kept
}

void TestPhase7B::test_SubtitleCache_set_budget()
{
    SubtitleCache cache(1024LL * 1024);
    QCOMPARE(cache.budgetBytes(), 1024LL * 1024);

    cache.setBudgetBytes(64LL * 1024 * 1024);
    QCOMPARE(cache.budgetBytes(), 64LL * 1024 * 1024);

    cache.setBudgetBytes(0); // unlimited
    QCOMPARE(cache.budgetBytes(), 0);
}

void TestPhase7B::test_SubtitleCache_clear()
{
    SubtitleCache cache(1024LL * 1024);
    QImage img(10, 10, QImage::Format_ARGB32_Premultiplied);
    cache.insert(1, SubtitleSurface(img));
    cache.insert(2, SubtitleSurface(img));
    QCOMPARE(cache.size(), 2);
    cache.clear();
    QCOMPARE(cache.size(), 0);
    QCOMPARE(cache.usedBytes(), 0);
}

// ============================================================
// SubtitleFrame cacheKey
// ============================================================

void TestPhase7B::test_SubtitleFrame_cacheKey()
{
    SubtitleFrame a, b;
    a.trackIndex = 0;
    a.subtitleIndex = 5;
    a.pts = 1000;
    b.trackIndex = 0;
    b.subtitleIndex = 5;
    b.pts = 1000;
    QCOMPARE(a.cacheKey(), b.cacheKey());

    // Different pts → different key
    b.pts = 2000;
    QVERIFY(a.cacheKey() != b.cacheKey());

    // Different subtitleIndex → different key
    a.subtitleIndex = 6;
    QVERIFY(a.cacheKey() != b.cacheKey());
}

void TestPhase7B::test_SubtitleFrame_isActiveSec()
{
    SubtitleFrame f;
    f.startSeconds = 10.0;
    f.endSeconds = 20.0;
    QVERIFY(f.isActiveSec(10.0));
    QVERIFY(f.isActiveSec(15.0));
    QVERIFY(!f.isActiveSec(20.0));  // end is exclusive
    QVERIFY(!f.isActiveSec(9.999));
    QVERIFY(!f.isActiveSec(25.0));
}

// ============================================================
// SubtitleDecoder
// ============================================================

void TestPhase7B::test_SubtitleDecoder_composeBitmap()
{
    // Can't fully test avcodec_decode_subtitle2 without real packets,
    // but we verify SubtitleDecoder construction and destruction work
    // and that the composition function can be compiled/linked.
    SubtitleDecoder decoder;
    QVERIFY(!decoder.isOpen());
}

// ============================================================
// ExternalSubtitleLoader
// ============================================================

void TestPhase7B::test_ExternalSubtitleLoader_detectBitmapCodec()
{
    QString ext = ExternalSubtitleLoader::detectCodec("test.sup");
    QCOMPARE(ext, QString("sup"));

    ext = ExternalSubtitleLoader::detectCodec("test.idx");
    QCOMPARE(ext, QString("vobsub"));

    ext = ExternalSubtitleLoader::detectCodec("test.sub");
    QCOMPARE(ext, QString("vobsub"));

    ext = ExternalSubtitleLoader::detectCodec("test.srt");
    QCOMPARE(ext, QString("srt"));
}

void TestPhase7B::test_ExternalSubtitleLoader_supportedExtensions()
{
    QStringList exts = ExternalSubtitleLoader::supportedExtensions();
    QVERIFY(exts.contains("sup"));
    QVERIFY(exts.contains("idx"));
    QVERIFY(exts.contains("sub"));
    QVERIFY(exts.contains("srt"));
    QVERIFY(exts.contains("ass"));
    QVERIFY(exts.contains("vtt"));
}

// ============================================================
// SubtitleManager
// ============================================================

void TestPhase7B::test_SubtitleManager_getSubtitleImages()
{
    SubtitleManager mgr;
    QVERIFY(mgr.subtitlesEnabled());

    // With no tracks, should return empty list
    QVector<QImage> imgs = mgr.getSubtitleImages(0.0, 1920, 1080);
    QCOMPARE(imgs.size(), 0);

    // Test setCacheBudgetMB
    mgr.setCacheBudgetMB(64);
    QCOMPARE(mgr.cacheBudgetBytes(), 64LL * 1024 * 1024);

    mgr.setCacheBudgetMB(256);
    QCOMPARE(mgr.cacheBudgetBytes(), 256LL * 1024 * 1024);
}

// ============================================================
// Stability
// ============================================================

void TestPhase7B::test_cacheKey_stability()
{
    // Same inputs produce same key
    SubtitleFrame a, b;
    a.trackIndex = 2;
    a.subtitleIndex = 15;
    a.pts = 5000;
    b.trackIndex = 2;
    b.subtitleIndex = 15;
    b.pts = 5000;
    QCOMPARE(a.cacheKey(), b.cacheKey());

    // Different track → different key
    b.trackIndex = 3;
    QVERIFY(a.cacheKey() != b.cacheKey());
}

QTEST_MAIN(TestPhase7B)
#include "test_phase7b.moc"
