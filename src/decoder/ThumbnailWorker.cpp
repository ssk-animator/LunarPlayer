#include "ThumbnailWorker.h"
#include <QDebug>

ThumbnailWorker::ThumbnailWorker(QObject *parent)
    : QObject(parent)
{
}

ThumbnailWorker::~ThumbnailWorker()
{
    close();
}

void ThumbnailWorker::configure(const QString &filePath, int videoStreamIdx,
                                 int srcWidth, int srcHeight, double durationSec)
{
    close();
    m_filePath = filePath;
    m_videoStreamIdx = videoStreamIdx;
    m_srcWidth = srcWidth;
    m_srcHeight = srcHeight;
    m_durationSec = durationSec;
    m_configured = true;
    m_stopRequested = false;
}

void ThumbnailWorker::stop()
{
    m_stopRequested = true;
    close();
}

void ThumbnailWorker::close()
{
    if (m_swsCtx) { sws_freeContext(m_swsCtx); m_swsCtx = nullptr; }
    if (m_codecCtx) { avcodec_free_context(&m_codecCtx); }
    if (m_fmtCtx) { avformat_close_input(&m_fmtCtx); }
}

bool ThumbnailWorker::ensureOpen()
{
    if (m_fmtCtx && m_codecCtx && m_swsCtx) return true;
    if (!m_configured) return false;

    if (avformat_open_input(&m_fmtCtx, m_filePath.toUtf8().constData(),
                             nullptr, nullptr) != 0) {
        qWarning() << "ThumbnailWorker: failed to open" << m_filePath;
        return false;
    }
    if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0) {
        avformat_close_input(&m_fmtCtx);
        return false;
    }

    const AVCodec *codec = avcodec_find_decoder(
        m_fmtCtx->streams[m_videoStreamIdx]->codecpar->codec_id);
    if (!codec) { close(); return false; }

    m_codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_codecCtx,
        m_fmtCtx->streams[m_videoStreamIdx]->codecpar);
    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        close();
        return false;
    }

    m_swsCtx = sws_getContext(
        m_srcWidth, m_srcHeight, m_codecCtx->pix_fmt,
        kThumbWidth, kThumbHeight, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_swsCtx) { close(); return false; }

    return true;
}

void ThumbnailWorker::generate(double timeSec)
{
    if (m_stopRequested || !m_configured) return;

    int64_t requestKey = qRound(timeSec);
    if (m_latestRequestKey >= 0 && requestKey != m_latestRequestKey.load())
        return;

    m_timer.start();
    if (!ensureOpen()) return;

    QImage img = decodeAt(timeSec);
    if (!img.isNull())
        emit thumbnailReady(timeSec, img);

    emit batchTimeMeasured(m_timer.elapsed());
}

QImage ThumbnailWorker::decodeAt(double timeSec)
{
    if (m_stopRequested) return QImage();

    int64_t targetTs = static_cast<int64_t>(timeSec * AV_TIME_BASE);
    av_seek_frame(m_fmtCtx, -1, targetTs, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(m_codecCtx);

    AVFrame *frame = av_frame_alloc();
    AVPacket *pkt = av_packet_alloc();
    QImage result;
    int attempts = 0;

    while (attempts < 60 && av_read_frame(m_fmtCtx, pkt) >= 0) {
        if (m_stopRequested) break;
        if (pkt->stream_index == m_videoStreamIdx) {
            if (avcodec_send_packet(m_codecCtx, pkt) == 0) {
                int ret = avcodec_receive_frame(m_codecCtx, frame);
                if (ret == 0) {
                    result = QImage(kThumbWidth, kThumbHeight, QImage::Format_RGB888);
                    uint8_t *dstData[1] = { result.bits() };
                    int dstLinesize[1] = { static_cast<int>(result.bytesPerLine()) };
                    sws_scale(m_swsCtx,
                              frame->data, frame->linesize,
                              0, m_srcHeight,
                              dstData, dstLinesize);
                    break;
                }
            }
        }
        av_packet_unref(pkt);
        attempts++;
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);
    return result;
}
