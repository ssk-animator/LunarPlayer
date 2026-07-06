#ifndef DECODETHREAD_H
#define DECODETHREAD_H

#include <QMutex>
#include <QWaitCondition>
#include <QElapsedTimer>
#include <deque>
#include <queue>
#include <atomic>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/mathematics.h>
}

struct DecodeFrame {
    AVFrame *frame = nullptr;
    double ptsSec = 0.0;
    int64_t frameNum = -1;
};

class DecodeThread {
public:
    DecodeThread();
    ~DecodeThread();

    bool open(const QString &path);
    void close();
    void startPlayback(int fps, double speed);
    void stopPlayback();
    void seek(double ptsSec);
    void setSkipRGBConversion(bool skip) { m_skipRGBConversion = skip; }

    bool popFrame(DecodeFrame &out);
    bool peekFrame(DecodeFrame &out) const;
    void dropFrontFrame();
    bool hasFrame() const;
    int queueSize() const;
    bool isRunning() const { return m_running; }

    int fps() const { return m_fps; }
    double lastDecodedPtsSec() const { return m_lastDecodedPtsSec; }
    bool isFinished() const { return m_finished; }
    double lastDemuxMs() const { return m_lastDemuxMs; }
    double lastDecodeMs() const { return m_lastDecodeMs; }

    int width() const { return m_width; }
    int height() const { return m_height; }
    AVPixelFormat pixFmt() const { return m_pixFmt; }
    AVCodecContext* codecContext() const { return m_codecCtx; }

private:
    void decodeLoop();
    bool decodeOnePacket();

    std::thread *m_thread = nullptr;
    AVFormatContext *m_fmtCtx = nullptr;
    AVCodecContext *m_codecCtx = nullptr;
    AVPacket *m_pkt = nullptr;
    AVFrame *m_decodedFrame = nullptr;
    AVFrame *m_hwFrame = nullptr;
    AVBufferRef *m_hwDeviceCtx = nullptr;

    int m_videoStreamIdx = -1;
    int m_audioStreamIdx = -1;
    int m_width = 0, m_height = 0;
    int m_fps = 24;
    AVPixelFormat m_pixFmt = AV_PIX_FMT_NONE;

    mutable QMutex m_mutex;
    QWaitCondition m_frameAvailable;
    QWaitCondition m_queueNotFull;
    std::deque<DecodeFrame> m_queue;
    int m_queueMax = 8;

    std::queue<AVPacket*> m_audioQueue;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_seekRequested{false};
    std::atomic<double> m_seekPts{0.0};
    bool m_skipRGBConversion = false;
    std::atomic<bool> m_finished{false};

    double m_lastDecodedPtsSec = 0.0;
    double m_lastDemuxMs = 0.0;
    double m_lastDecodeMs = 0.0;
    QElapsedTimer m_demuxTimer;
    QElapsedTimer m_decodeTimer;
};

#endif
