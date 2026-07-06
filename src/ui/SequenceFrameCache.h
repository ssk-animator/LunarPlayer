#ifndef SEQUENCEFRAMECACHE_H
#define SEQUENCEFRAMECACHE_H

#include <QObject>
#include <QImage>
#include <QMap>
#include <QList>
#include <QMutex>
#include <QThread>
#include <QAtomicInt>
#include <QWaitCondition>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

class SequenceFrameWorker;

class SequenceFrameCache : public QObject
{
    Q_OBJECT
public:
    explicit SequenceFrameCache(QObject *parent = nullptr);
    ~SequenceFrameCache() override;

    void configure(const QString &pattern, int startFrame, int totalFrames,
                   int width, int height, AVPixelFormat pixFmt);
    void clear();
    bool isConfigured() const { return m_configured; }

    QImage frame(int frameNum);
    bool hasFrame(int frameNum) const;
    void prefetch(int centerFrame, int radius);

    int cachedFrames() const { QMutexLocker l(&m_mutex); return m_cache.size(); }
    int cacheMemoryBytes() const { QMutexLocker l(&m_mutex); return m_currentBytes; }
    int totalFrames() const { return m_totalFrames; }
    int startFrame() const { return m_startFrame; }
    QString pattern() const { return m_pattern; }

signals:
    void frameReady(int frameNum);

private:
    friend class SequenceFrameWorker;
    QImage decodeSingleFrame(int frameNum);
    void insertCache(int frameNum, const QImage &img);
    void evictToSize(int targetBytes);

    QString m_pattern;
    int m_startFrame = 0;
    int m_totalFrames = 0;
    int m_width = 0;
    int m_height = 0;
    AVPixelFormat m_pixFmt = AV_PIX_FMT_NONE;
    bool m_configured = false;

    mutable QMutex m_mutex;
    QMap<int, QImage> m_cache;
    QList<int> m_lruOrder;
    int m_currentBytes = 0;
    int m_maxBytes = 256 * 1024 * 1024;

    QThread *m_workerThread = nullptr;
    SequenceFrameWorker *m_worker = nullptr;
    QAtomicInt m_prefetchCenter{-1};
    QAtomicInt m_prefetchRadius{0};
    QWaitCondition m_prefetchWake;
};

class SequenceFrameWorker : public QObject
{
    Q_OBJECT
public:
    explicit SequenceFrameWorker(SequenceFrameCache *cache);
    void requestPrefetch(int center, int radius);
    void requestStop();

public slots:
    void process();

private:
    SequenceFrameCache *m_cache;
    int m_requestedCenter = -1;
    int m_requestedRadius = 0;
    QMutex m_reqMutex;
    bool m_stopRequested = false;
};

#endif // SEQUENCEFRAMECACHE_H
