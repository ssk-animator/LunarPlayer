#ifndef THUMBNAILWORKER_H
#define THUMBNAILWORKER_H

#include <QObject>
#include <QImage>
#include <QElapsedTimer>
#include <atomic>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

class ThumbnailWorker : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailWorker(QObject *parent = nullptr);
    ~ThumbnailWorker();

    void requestStop() { m_stopRequested = true; }
    void setLatestRequestKey(int64_t key) { m_latestRequestKey.store(key); }

public slots:
    void configure(const QString &filePath, int videoStreamIdx,
                   int srcWidth, int srcHeight, double durationSec);
    void generate(double timeSec);
    void stop();

signals:
    void thumbnailReady(double timeSec, QImage image);
    void batchTimeMeasured(double timeMs);

private:
    bool ensureOpen();
    void close();
    QImage decodeAt(double timeSec);

    QString m_filePath;
    int m_videoStreamIdx = -1;
    int m_srcWidth = 0;
    int m_srcHeight = 0;
    double m_durationSec = 0.0;
    bool m_configured = false;
    std::atomic<bool> m_stopRequested{false};

    AVFormatContext *m_fmtCtx = nullptr;
    AVCodecContext *m_codecCtx = nullptr;
    SwsContext *m_swsCtx = nullptr;

    static constexpr int kThumbWidth = 180;
    static constexpr int kThumbHeight = 101;

    QElapsedTimer m_timer;
    std::atomic<int64_t> m_latestRequestKey{-1};
};

#endif
