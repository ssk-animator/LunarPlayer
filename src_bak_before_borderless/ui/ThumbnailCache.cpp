#include "ThumbnailCache.h"
#include "decoder/ThumbnailWorker.h"
#include <QDebug>

ThumbnailCache::ThumbnailCache(QObject *parent)
    : QObject(parent)
{
}

ThumbnailCache::~ThumbnailCache()
{
    stop();
}

void ThumbnailCache::stop()
{
    m_stopRequested = true;

    if (m_worker) {
        disconnect(m_worker, nullptr, this, nullptr);
        m_worker->requestStop();
    }

    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(2000);
    }

    delete m_worker;
    m_worker = nullptr;

    if (m_workerThread) {
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }
}

void ThumbnailCache::configure(const QString &filePath, int videoStreamIdx,
                                int width, int height, double durationSec)
{
    m_stopRequested = true;

    if (m_worker) {
        disconnect(m_worker, nullptr, this, nullptr);
        m_worker->requestStop();
    }
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(2000);
    }
    delete m_worker;
    m_worker = nullptr;
    if (m_workerThread) {
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }

    m_filePath = filePath;
    m_videoStreamIdx = videoStreamIdx;
    m_width = width;
    m_height = height;
    m_durationSec = durationSec;

    {
        QMutexLocker lock(&m_mutex);
        m_cache.clear();
        m_lruOrder.clear();
        m_lruIterMap.clear();
        m_currentCacheBytes = 0;
    }
    m_pending.clear();
    m_lastRequestTime = -1.0;
    m_stopRequested = false;

    m_workerThread = new QThread(this);
    m_worker = new ThumbnailWorker();
    m_worker->moveToThread(m_workerThread);

    connect(m_worker, &ThumbnailWorker::thumbnailReady,
            this, &ThumbnailCache::onThumbnailGenerated,
            Qt::QueuedConnection);
    connect(m_worker, &ThumbnailWorker::batchTimeMeasured,
            this, &ThumbnailCache::onBatchTimeMeasured,
            Qt::QueuedConnection);

    QMetaObject::invokeMethod(m_worker, "configure", Qt::QueuedConnection,
        Q_ARG(QString, filePath),
        Q_ARG(int, videoStreamIdx),
        Q_ARG(int, width),
        Q_ARG(int, height),
        Q_ARG(double, durationSec));

    m_workerThread->start();
    m_ready = true;
}

QImage ThumbnailCache::thumbnail(double timeSec)
{
    double key = qRound(timeSec);

    QImage cached = lookupCache(key);
    if (!cached.isNull()) {
        m_cacheHits++;
        return cached;
    }

    m_cacheMisses++;

    if (!m_stopRequested && !m_pending.contains(key)) {
        if (m_worker)
            m_worker->setLatestRequestKey(static_cast<int64_t>(key));
        m_lastRequestTime = key;
        m_pending.insert(key);
        QMetaObject::invokeMethod(m_worker, "generate", Qt::QueuedConnection,
            Q_ARG(double, key));
    }

    QMutexLocker lock(&m_mutex);
    if (m_cache.isEmpty()) return QImage();
    auto it = m_cache.lowerBound(key);
    if (it == m_cache.end()) return m_cache.last();
    if (it == m_cache.begin()) return *it;
    auto prev = it - 1;
    return (qAbs(prev.key() - key) <= qAbs(it.key() - key)) ? *prev : *it;
}

void ThumbnailCache::onThumbnailGenerated(double timeSec, QImage image)
{
    if (image.isNull() || m_stopRequested) return;

    double key = qRound(timeSec);
    insertCache(key, image);
    m_pending.remove(key);
    emit thumbnailReady(timeSec);
}

void ThumbnailCache::onBatchTimeMeasured(double timeMs)
{
    m_lastBatchTimeMs = timeMs;
}

QImage ThumbnailCache::lookupCache(double key)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_cache.find(key);
    if (it == m_cache.end()) return QImage();

    auto lruIt = m_lruIterMap.value(key);
    if (lruIt != m_lruOrder.end()) {
        m_lruOrder.erase(lruIt);
    }
    m_lruOrder.push_front(key);
    m_lruIterMap[key] = m_lruOrder.begin();

    return *it;
}

void ThumbnailCache::insertCache(double key, const QImage &img)
{
    QMutexLocker lock(&m_mutex);

    int bytes = imageBytes(img);

    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        m_currentCacheBytes -= imageBytes(*it);
        auto lruIt = m_lruIterMap.value(key);
        if (lruIt != m_lruOrder.end()) {
            m_lruOrder.erase(lruIt);
        }
        m_lruIterMap.remove(key);
        m_cache.erase(it);
    }

    evictToSize(m_maxCacheBytes - bytes);

    m_cache[key] = img;
    m_lruOrder.push_front(key);
    m_lruIterMap[key] = m_lruOrder.begin();
    m_currentCacheBytes += bytes;
}

void ThumbnailCache::evictToSize(int targetBytes)
{
    while (m_currentCacheBytes > targetBytes && !m_lruOrder.empty()) {
        double oldestKey = m_lruOrder.back();
        m_lruOrder.pop_back();

        auto it = m_cache.find(oldestKey);
        if (it != m_cache.end()) {
            m_currentCacheBytes -= imageBytes(*it);
            m_cache.erase(it);
        }
        m_lruIterMap.remove(oldestKey);
    }
}
