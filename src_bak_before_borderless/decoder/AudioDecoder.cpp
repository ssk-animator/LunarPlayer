#include "AudioDecoder.h"

AudioDecoder::AudioDecoder() {}

AudioDecoder::~AudioDecoder()
{
    close();
}

bool AudioDecoder::open(AVFormatContext *fmtCtx, int streamIdx)
{
    close();

    if (!fmtCtx || streamIdx < 0 || streamIdx >= (int)fmtCtx->nb_streams)
        return false;

    AVStream *st = fmtCtx->streams[streamIdx];
    if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
        return false;

    m_codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!m_codec) return false;

    m_codecCtx = avcodec_alloc_context3(m_codec);
    if (!m_codecCtx) return false;

    if (avcodec_parameters_to_context(m_codecCtx, st->codecpar) < 0) {
        avcodec_free_context(&m_codecCtx);
        return false;
    }

    if (avcodec_open2(m_codecCtx, m_codec, nullptr) < 0) {
        avcodec_free_context(&m_codecCtx);
        return false;
    }

    m_frame = av_frame_alloc();
    if (!m_frame) {
        avcodec_free_context(&m_codecCtx);
        return false;
    }

    m_streamIdx = streamIdx;

    // Set up resampler to 48000Hz stereo float
    AVChannelLayout outChLayout = AV_CHANNEL_LAYOUT_STEREO;
    if (m_outputChannels == 1)
        outChLayout = AV_CHANNEL_LAYOUT_MONO;

    swr_alloc_set_opts2(&m_swrCtx,
        &outChLayout, AV_SAMPLE_FMT_FLT, m_outputSampleRate,
        &m_codecCtx->ch_layout, m_codecCtx->sample_fmt, m_codecCtx->sample_rate,
        0, nullptr);

    if (!m_swrCtx || swr_init(m_swrCtx) < 0) {
        swr_free(&m_swrCtx);
        avcodec_free_context(&m_codecCtx);
        m_streamIdx = -1;
        return false;
    }

    return true;
}

void AudioDecoder::close()
{
    swr_free(&m_swrCtx);
    av_frame_free(&m_frame);
    avcodec_free_context(&m_codecCtx);
    m_codec = nullptr;
    m_streamIdx = -1;
}

bool AudioDecoder::isOpen() const
{
    return m_codecCtx != nullptr;
}

int AudioDecoder::decode(const AVPacket *pkt, float *outBuffer, int maxFrames)
{
    if (!m_codecCtx || !m_frame) return -1;

    if (avcodec_send_packet(m_codecCtx, pkt) < 0)
        return 0;

    int totalFrames = 0;
    while (totalFrames < maxFrames) {
        int ret = avcodec_receive_frame(m_codecCtx, m_frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0)
            break;

        // Resample to output format
        int outFrames = swr_convert(m_swrCtx,
            (uint8_t**)&outBuffer, maxFrames - totalFrames,
            (const uint8_t**)m_frame->data, m_frame->nb_samples);

        if (outFrames > 0) {
            totalFrames += outFrames;
            outBuffer += outFrames * m_outputChannels;
        }

        av_frame_unref(m_frame);
    }
    return totalFrames;
}

void AudioDecoder::flush()
{
    if (m_codecCtx)
        avcodec_flush_buffers(m_codecCtx);
    if (m_swrCtx)
        swr_close(m_swrCtx);
    if (m_swrCtx)
        swr_init(m_swrCtx);
}
