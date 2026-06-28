#ifndef THUMBNAILCACHE_H
#define THUMBNAILCACHE_H

#include <QObject>
#include <QImage>
#include <QMap>
#include <QSet>
#include <QThread>
#include <QMutex>
#include <QElapsedTimer>
#include <list>
#include <atomic>

class ThumbnailWorker;

class ThumbnailCache : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailCache(QObject *parent = nullptr);
    ~ThumbnailCache();

    void configure(const QString &filePath, int videoStreamIdx,
                   int width, int height, double durationSec);
    void stop();

    QImage thumbnail(double timeSec);
    bool isReady() const { return m_ready; }

    // Instrumentation
    int cacheHits() const { return m_cacheHits.load(); }
    int cacheMisses() const { return m_cacheMisses.load(); }
    int cacheEntryCount() const { return m_cache.size(); }
    int cacheBytes() const { return m_currentCacheBytes; }
    int cacheMaxBytes() const { return m_maxCacheBytes; }
    double lastBatchTimeMs() const { return m_lastBatchTimeMs.load(); }
    void resetStats() { m_cacheHits = 0; m_cacheMisses = 0; }

signals:
    void thumbnailReady(double timeSec);

private slots:
    void onThumbnailGenerated(double timeSec, QImage image);
    void onBatchTimeMeasured(double timeMs);

private:
    QImage lookupCache(double key);
    void insertCache(double key, const QImage &img);
    void evictToSize(int targetBytes);
    static int imageBytes(const QImage &img) { return img.sizeInBytes(); }

    QString m_filePath;
    int m_videoStreamIdx = -1;
    int m_width = 0;
    int m_height = 0;
    double m_durationSec = 0.0;

    // LRU cache
    QMap<double, QImage> m_cache;
    std::list<double> m_lruOrder;
    QMap<double, std::list<double>::iterator> m_lruIterMap;
    int m_maxCacheBytes = 50 * 1024 * 1024;
    int m_currentCacheBytes = 0;
    mutable QMutex m_mutex;

    std::atomic<bool> m_ready{false};
    std::atomic<bool> m_stopRequested{false};

    // Worker thread
    QThread *m_workerThread = nullptr;
    ThumbnailWorker *m_worker = nullptr;

    // Pending tracking
    QSet<double> m_pending;
    double m_lastRequestTime = -1.0;

    // Instrumentation
    std::atomic<int> m_cacheHits{0};
    std::atomic<int> m_cacheMisses{0};
    std::atomic<double> m_lastBatchTimeMs{0.0};
};

#endif
