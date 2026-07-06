#include "NetworkMediaSession.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#include <QUrl>
#include <QFileInfo>
#include <algorithm>

static int interruptCallback(void *ctx)
{
    auto *flag = static_cast<std::atomic<bool>*>(ctx);
    return flag->load() ? 1 : 0;
}

NetworkMediaSession::NetworkMediaSession(QObject *parent)
    : QObject(parent)
{
}

NetworkMediaSession::~NetworkMediaSession()
{
    close();
}

bool NetworkMediaSession::isNetworkUrl(const QString &url)
{
    // All known network protocol schemes
    if (url.startsWith("http://", Qt::CaseInsensitive) ||
        url.startsWith("https://", Qt::CaseInsensitive) ||
        url.startsWith("rtsp://", Qt::CaseInsensitive) ||
        url.startsWith("rtmp://", Qt::CaseInsensitive) ||
        url.startsWith("ftp://", Qt::CaseInsensitive) ||
        url.startsWith("mms://", Qt::CaseInsensitive) ||
        url.startsWith("srt://", Qt::CaseInsensitive) ||
        url.startsWith("udp://", Qt::CaseInsensitive) ||
        url.startsWith("tcp://", Qt::CaseInsensitive))
        return true;
    return false;
}

QString NetworkMediaSession::protocolName(const QString &url)
{
    int idx = url.indexOf("://");
    if (idx > 0) return url.left(idx).toUpper();
    return "FILE";
}

bool NetworkMediaSession::openStream(const QString &url)
{
    close();
    m_url = url;
    m_lastError.clear();
    m_isLive = false;
    m_interrupt.store(false);

    m_fmtCtx = avformat_alloc_context();
    if (!m_fmtCtx) {
        m_lastError = "Failed to allocate format context";
        emit streamOpened(false);
        return false;
    }

    m_fmtCtx->interrupt_callback.callback = interruptCallback;
    m_fmtCtx->interrupt_callback.opaque = &m_interrupt;

    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "timeout", "10000000", 0);
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    av_dict_set(&opts, "max_delay", "500000", 0);
    av_dict_set(&opts, "reconnect", "1", 0);
    av_dict_set(&opts, "reconnect_at_eof", "1", 0);
    av_dict_set(&opts, "reconnect_streamed", "1", 0);
    av_dict_set(&opts, "reconnect_delay_max", "5", 0);

    int ret = avformat_open_input(&m_fmtCtx, url.toUtf8().constData(), nullptr, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        char err[256] = {};
        av_strerror(ret, err, sizeof(err));
        m_lastError = QString("Failed to open stream: %1").arg(err);
        avformat_close_input(&m_fmtCtx);
        emit streamOpened(false);
        return false;
    }

    ret = avformat_find_stream_info(m_fmtCtx, nullptr);
    if (ret < 0) {
        m_lastError = "Failed to find stream info";
        avformat_close_input(&m_fmtCtx);
        emit streamOpened(false);
        return false;
    }

    m_durationSec = (m_fmtCtx->duration > 0)
        ? static_cast<double>(m_fmtCtx->duration) / AV_TIME_BASE
        : 0.0;
    m_isLive = (m_durationSec <= 0.0 || m_fmtCtx->pb->seekable == 0);

    // Find best video stream
    int videoIdx = av_find_best_stream(m_fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoIdx < 0) {
        m_lastError = "No video stream found";
        avformat_close_input(&m_fmtCtx);
        emit streamOpened(false);
        return false;
    }

    if (!openCodec(videoIdx)) {
        avformat_close_input(&m_fmtCtx);
        emit streamOpened(false);
        return false;
    }

    // Find best audio stream (non-blocking if none)
    int audioIdx = av_find_best_stream(m_fmtCtx, AVMEDIA_TYPE_AUDIO, -1, videoIdx, nullptr, 0);
    if (audioIdx >= 0)
        openAudioDecoder(audioIdx);

    // Set total bytes for buffer progress (estimate from bitrate * duration for non-live)
    if (!m_isLive && m_durationSec > 0.0 && m_codecCtx->bit_rate > 0) {
        m_buffer.setTotalBytes(static_cast<int64_t>(m_codecCtx->bit_rate / 8 * m_durationSec));
    }
    m_buffer.setState(NetworkBuffer::Ready);

    emit streamOpened(true);
    return true;
}

void NetworkMediaSession::close()
{
    m_interrupt.store(true);
    if (m_swsCtx) { sws_freeContext(m_swsCtx); m_swsCtx = nullptr; }
    if (m_swrCtx) { swr_free(&m_swrCtx); }
    if (m_codecCtx) { avcodec_free_context(&m_codecCtx); }
    if (m_audioCodecCtx) { avcodec_free_context(&m_audioCodecCtx); }
    if (m_fmtCtx) { avformat_close_input(&m_fmtCtx); }
    av_packet_free(&m_pkt);
    av_frame_free(&m_decodedFrame);
    m_frame = QImage();
    m_audioSampleBuffer.clear();
    m_videoStreamIdx = -1;
    m_audioStreamIdx = -1;
    m_width = m_height = 0;
    m_audioSampleRate = 0;
    m_audioChannels = 0;
    m_audioClock = 0.0;
    m_fps = 24.0;
    m_buffer.reset();
}

bool NetworkMediaSession::openCodec(int streamIndex)
{
    const AVCodecParameters *par = m_fmtCtx->streams[streamIndex]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    if (!codec) {
        m_lastError = "No decoder found";
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        m_lastError = "Failed to allocate codec context";
        return false;
    }

    int ret = avcodec_parameters_to_context(m_codecCtx, par);
    if (ret < 0) {
        m_lastError = "Failed to copy codec parameters";
        return false;
    }

    ret = avcodec_open2(m_codecCtx, codec, nullptr);
    if (ret < 0) {
        m_lastError = "Failed to open decoder";
        return false;
    }

    m_videoStreamIdx = streamIndex;
    m_width = m_codecCtx->width;
    m_height = m_codecCtx->height;

    AVStream *st = m_fmtCtx->streams[streamIndex];
    if (st->avg_frame_rate.den > 0)
        m_fps = static_cast<double>(st->avg_frame_rate.num) / st->avg_frame_rate.den;
    else if (st->r_frame_rate.den > 0)
        m_fps = static_cast<double>(st->r_frame_rate.num) / st->r_frame_rate.den;

    m_pkt = av_packet_alloc();
    m_decodedFrame = av_frame_alloc();

    return true;
}

bool NetworkMediaSession::openAudioDecoder(int streamIndex)
{
    const AVCodecParameters *par = m_fmtCtx->streams[streamIndex]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    if (!codec) return false;

    m_audioCodecCtx = avcodec_alloc_context3(codec);
    if (!m_audioCodecCtx) return false;

    int ret = avcodec_parameters_to_context(m_audioCodecCtx, par);
    if (ret < 0) return false;

    ret = avcodec_open2(m_audioCodecCtx, codec, nullptr);
    if (ret < 0) return false;

    m_audioStreamIdx = streamIndex;
    m_audioSampleRate = m_audioCodecCtx->sample_rate;
    m_audioChannels = m_audioCodecCtx->ch_layout.nb_channels;

    // Set up audio resampler (convert to float planar)
    m_swrCtx = swr_alloc();
    if (!m_swrCtx) return false;

    AVChannelLayout outChLayout;
    av_channel_layout_default(&outChLayout, m_audioChannels);

    ret = swr_alloc_set_opts2(&m_swrCtx,
        &outChLayout, AV_SAMPLE_FMT_FLT, m_audioSampleRate,
        &m_audioCodecCtx->ch_layout, m_audioCodecCtx->sample_fmt, m_audioCodecCtx->sample_rate,
        0, nullptr);
    if (ret < 0 || !m_swrCtx) return false;

    ret = swr_init(m_swrCtx);
    if (ret < 0) return false;

    return true;
}

void NetworkMediaSession::flushAudioDecoder()
{
    if (!m_audioCodecCtx) return;
    avcodec_send_packet(m_audioCodecCtx, nullptr);
    AVFrame *audioFrame = av_frame_alloc();
    while (avcodec_receive_frame(m_audioCodecCtx, audioFrame) == 0) {
        processAudioFrame(audioFrame);
    }
    av_frame_free(&audioFrame);
}

bool NetworkMediaSession::readFrame()
{
    if (!m_fmtCtx || !m_codecCtx) return false;

    m_decodeTimer.restart();

    while (true) {
        int ret = av_read_frame(m_fmtCtx, m_pkt);
        if (ret < 0) {
            // Flush video decoder
            avcodec_send_packet(m_codecCtx, nullptr);
            ret = avcodec_receive_frame(m_codecCtx, m_decodedFrame);
            if (ret == 0) {
                processDecodedFrame(m_decodedFrame);
                m_lastDecodeMs = m_decodeTimer.elapsed();
                return true;
            }
            // Also flush audio decoder
            flushAudioDecoder();
            return false;
        }

        if (m_pkt->stream_index == m_videoStreamIdx) {
            ret = avcodec_send_packet(m_codecCtx, m_pkt);
            if (ret < 0) {
                av_packet_unref(m_pkt);
                continue;
            }
            ret = avcodec_receive_frame(m_codecCtx, m_decodedFrame);
            if (ret == 0) {
                m_buffer.recordBytes(m_pkt->size);
                processDecodedFrame(m_decodedFrame);
                av_packet_unref(m_pkt);
                m_lastDecodeMs = m_decodeTimer.elapsed();
                return true;
            }
        } else if (m_audioStreamIdx >= 0 && m_pkt->stream_index == m_audioStreamIdx) {
            ret = avcodec_send_packet(m_audioCodecCtx, m_pkt);
            if (ret == 0) {
                AVFrame *audioFrame = av_frame_alloc();
                ret = avcodec_receive_frame(m_audioCodecCtx, audioFrame);
                if (ret == 0) {
                    processAudioFrame(audioFrame);
                }
                av_frame_free(&audioFrame);
            }
        }
        av_packet_unref(m_pkt);
    }
}

void NetworkMediaSession::processDecodedFrame(AVFrame *frame)
{
    if (!m_swsCtx || m_width != frame->width || m_height != frame->height) {
        if (m_swsCtx) sws_freeContext(m_swsCtx);
        m_swsCtx = sws_getContext(
            frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
            frame->width, frame->height, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        m_width = frame->width;
        m_height = frame->height;
    }

    QImage img(frame->width, frame->height, QImage::Format_RGB888);
    uint8_t *dst[] = { img.bits() };
    int dstStride[] = { static_cast<int>(img.bytesPerLine()) };

    sws_scale(m_swsCtx, frame->data, frame->linesize, 0, frame->height, dst, dstStride);
    m_frame = img.copy();
}

void NetworkMediaSession::processAudioFrame(AVFrame *frame)
{
    if (!m_swrCtx) return;

    int outSamples = swr_get_out_samples(m_swrCtx, frame->nb_samples);
    if (outSamples <= 0) return;

    QVector<float> tempBuf(static_cast<size_t>(outSamples) * m_audioChannels);

    uint8_t *outBuf[2] = {
        reinterpret_cast<uint8_t*>(tempBuf.data()),
        nullptr
    };

    int samplesConverted = swr_convert(m_swrCtx,
        outBuf, outSamples,
        const_cast<const uint8_t**>(frame->data), frame->nb_samples);

    if (samplesConverted > 0) {
        int oldSize = m_audioSampleBuffer.size();
        int newSamples = samplesConverted * m_audioChannels;
        m_audioSampleBuffer.resize(oldSize + newSamples);
        memcpy(m_audioSampleBuffer.data() + oldSize, tempBuf.data(),
               static_cast<size_t>(newSamples) * sizeof(float));
    }
}

bool NetworkMediaSession::popAudioSamples(QVector<float> &samples)
{
    if (m_audioSampleBuffer.isEmpty())
        return false;
    samples = m_audioSampleBuffer;
    m_audioSampleBuffer.clear();
    return true;
}

double NetworkMediaSession::audioClock() const
{
    return m_audioClock;
}

bool NetworkMediaSession::seekSec(double sec)
{
    if (!m_fmtCtx || m_isLive) return false;
    int64_t ts = static_cast<int64_t>(sec / m_durationSec * m_fmtCtx->duration);
    int ret = av_seek_frame(m_fmtCtx, -1, ts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) return false;
    if (m_codecCtx) avcodec_flush_buffers(m_codecCtx);
    if (m_audioCodecCtx) avcodec_flush_buffers(m_audioCodecCtx);
    m_audioSampleBuffer.clear();
    m_audioClock = sec;
    return true;
}

double NetworkMediaSession::currentPtsSec() const
{
    if (!m_fmtCtx || m_videoStreamIdx < 0) return 0.0;
    AVStream *st = m_fmtCtx->streams[m_videoStreamIdx];
    if (st && m_decodedFrame && m_decodedFrame->pts != AV_NOPTS_VALUE)
        return static_cast<double>(m_decodedFrame->pts) * av_q2d(st->time_base);
    return 0.0;
}

HDRMetadata NetworkMediaSession::hdrMetadata() const
{
    HDRMetadata md;
    if (!m_codecCtx) return md;

    switch (m_codecCtx->color_trc) {
    case AVCOL_TRC_SMPTE2084:
        md.transfer = TransferCharacteristics::PQ;
        md.format = HDRFormat::HDR10;
        break;
    case AVCOL_TRC_ARIB_STD_B67:
        md.transfer = TransferCharacteristics::HLG;
        md.format = HDRFormat::HLG;
        break;
    case AVCOL_TRC_BT709:
        md.transfer = TransferCharacteristics::BT709;
        md.format = HDRFormat::SDR;
        break;
    default:
        md.transfer = TransferCharacteristics::Unknown;
        md.format = HDRFormat::None;
        break;
    }

    switch (m_codecCtx->color_primaries) {
    case AVCOL_PRI_BT709:
        md.primaries = ColorPrimaries::BT709;
        break;
    case AVCOL_PRI_BT2020:
        md.primaries = ColorPrimaries::BT2020;
        break;
    case AVCOL_PRI_SMPTE432:
        md.primaries = ColorPrimaries::DCI_P3;
        break;
    default:
        md.primaries = ColorPrimaries::Unknown;
        break;
    }

    return md;
}
