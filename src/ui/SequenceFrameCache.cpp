#include "SequenceFrameCache.h"
#include <QFile>
#include <QMutexLocker>
#include <QDebug>

// ============================================================
// SequenceFrameWorker
// ============================================================

SequenceFrameWorker::SequenceFrameWorker(SequenceFrameCache *cache)
    : m_cache(cache) {}

void SequenceFrameWorker::requestPrefetch(int center, int radius)
{
    QMutexLocker l(&m_reqMutex);
    m_requestedCenter = center;
    m_requestedRadius = radius;
}

void SequenceFrameWorker::requestStop()
{
    QMutexLocker l(&m_reqMutex);
    m_stopRequested = true;
}

void SequenceFrameWorker::process()
{
    while (true) {
        m_reqMutex.lock();
        while (m_requestedCenter < 0 && !m_stopRequested)
            m_cache->m_prefetchWake.wait(&m_reqMutex);
        if (m_stopRequested) {
            m_reqMutex.unlock();
            break;
        }
        int center = m_requestedCenter;
        int radius = m_requestedRadius;
        m_requestedCenter = -1;
        m_reqMutex.unlock();

        if (center >= 0) {
            int start = qMax(m_cache->m_startFrame, center - radius);
            int end = qMin(m_cache->m_startFrame + m_cache->m_totalFrames - 1, center + radius);

            for (int d = 0; d <= radius; ++d) {
                for (int sign : { -1, 1 }) {
                    if (m_stopRequested) break;
                    int fn = center + sign * d;
                    if (fn < start || fn > end) continue;
                    if (sign == -1 && d == 0) continue;
                    {
                        QMutexLocker lk(&m_cache->m_mutex);
                        if (m_cache->m_cache.contains(fn)) continue;
                    }
                    QImage img = m_cache->decodeSingleFrame(fn);
                    if (!img.isNull()) {
                        m_cache->insertCache(fn, img);
                        emit m_cache->frameReady(fn);
                    }
                }
                if (m_stopRequested) break;
            }
        }
    }
}

// ============================================================
// SequenceFrameCache
// ============================================================

SequenceFrameCache::SequenceFrameCache(QObject *parent)
    : QObject(parent)
{
    m_worker = new SequenceFrameWorker(this);
    m_workerThread = new QThread(this);
    m_worker->moveToThread(m_workerThread);
    connect(m_workerThread, &QThread::started, m_worker, &SequenceFrameWorker::process);
    m_workerThread->start();
}

SequenceFrameCache::~SequenceFrameCache()
{
    clear();
    if (m_workerThread && m_worker) {
        m_worker->requestStop();
        m_prefetchWake.wakeAll();
        m_workerThread->quit();
        if (!m_workerThread->wait(3000))
            qWarning("SequenceFrameCache: worker thread did not stop in 3s");
    }
}

void SequenceFrameCache::configure(const QString &pattern, int startFrame, int totalFrames,
                                    int width, int height, AVPixelFormat pixFmt)
{
    clear();
    m_pattern = pattern;
    m_startFrame = startFrame;
    m_totalFrames = totalFrames;
    m_width = width;
    m_height = height;
    m_pixFmt = pixFmt;
    m_configured = true;
}

void SequenceFrameCache::clear()
{
    QMutexLocker l(&m_mutex);
    m_cache.clear();
    m_lruOrder.clear();
    m_currentBytes = 0;
    m_configured = false;
    m_pattern.clear();
    m_totalFrames = 0;
    m_width = 0;
    m_height = 0;
    m_pixFmt = AV_PIX_FMT_NONE;
}

bool SequenceFrameCache::hasFrame(int frameNum) const
{
    QMutexLocker l(&m_mutex);
    return m_cache.contains(frameNum);
}

QImage SequenceFrameCache::frame(int frameNum)
{
    {
        QMutexLocker l(&m_mutex);
        auto it = m_cache.find(frameNum);
        if (it != m_cache.end()) {
            m_lruOrder.removeOne(frameNum);
            m_lruOrder.prepend(frameNum);
            return it.value();
        }
    }

    QImage img = decodeSingleFrame(frameNum);
    if (!img.isNull())
        insertCache(frameNum, img);
    return img;
}

void SequenceFrameCache::prefetch(int centerFrame, int radius)
{
    if (!m_configured || m_totalFrames <= 0) return;
    m_prefetchCenter.storeRelaxed(centerFrame);
    m_prefetchRadius.storeRelaxed(radius);
    m_worker->requestPrefetch(centerFrame, radius);
    m_prefetchWake.wakeOne();
}

QImage SequenceFrameCache::decodeSingleFrame(int frameNum)
{
    int actualNum = m_startFrame + frameNum;
    QString path = QString::asprintf(qPrintable(m_pattern), actualNum);
    if (!QFile::exists(path)) return {};

    AVFormatContext *fmtCtx = nullptr;
    if (avformat_open_input(&fmtCtx, path.toUtf8().constData(), nullptr, nullptr) < 0)
        return {};
    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        avformat_close_input(&fmtCtx);
        return {};
    }

    int vsi = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vsi < 0) { avformat_close_input(&fmtCtx); return {}; }

    const AVCodec *codec = avcodec_find_decoder(fmtCtx->streams[vsi]->codecpar->codec_id);
    if (!codec) { avformat_close_input(&fmtCtx); return {}; }

    AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) { avformat_close_input(&fmtCtx); return {}; }
    avcodec_parameters_to_context(codecCtx, fmtCtx->streams[vsi]->codecpar);
    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return {};
    }

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    QImage result;

    if (av_read_frame(fmtCtx, pkt) >= 0 && pkt->stream_index == vsi) {
        if (avcodec_send_packet(codecCtx, pkt) == 0) {
            if (avcodec_receive_frame(codecCtx, frame) == 0) {
                SwsContext *sws = sws_getContext(frame->width, frame->height,
                    (AVPixelFormat)frame->format,
                    frame->width, frame->height, AV_PIX_FMT_RGB24,
                    SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (sws) {
                    result = QImage(frame->width, frame->height, QImage::Format_RGB888);
                    uint8_t *dst[1] = { result.bits() };
                    int dstStride[1] = { static_cast<int>(result.bytesPerLine()) };
                    sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dstStride);
                    sws_freeContext(sws);
                }
            }
        }
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);

    return result;
}

void SequenceFrameCache::insertCache(int frameNum, const QImage &img)
{
    QMutexLocker l(&m_mutex);
    int bytes = img.sizeInBytes();
    if (bytes > m_maxBytes) return;

    evictToSize(m_maxBytes - bytes);
    m_cache.insert(frameNum, img);
    m_lruOrder.prepend(frameNum);
    m_currentBytes += bytes;
}

void SequenceFrameCache::evictToSize(int targetBytes)
{
    while (m_currentBytes > targetBytes && !m_lruOrder.isEmpty()) {
        int last = m_lruOrder.takeLast();
        auto it = m_cache.find(last);
        if (it != m_cache.end()) {
            m_currentBytes -= it->sizeInBytes();
            m_cache.erase(it);
        }
    }
}
