#ifndef AUDIOSTREAMDECODER_H
#define AUDIOSTREAMDECODER_H

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
}

struct DecodedAudioFrame {
    AVFrame *frame = nullptr;
    double ptsSec = 0.0;
    bool valid = false;

    ~DecodedAudioFrame() { if (frame) av_frame_free(&frame); }
    DecodedAudioFrame() { frame = av_frame_alloc(); }

    DecodedAudioFrame(const DecodedAudioFrame &) = delete;
    DecodedAudioFrame &operator=(const DecodedAudioFrame &) = delete;
    DecodedAudioFrame(DecodedAudioFrame &&o) noexcept : frame(o.frame), ptsSec(o.ptsSec), valid(o.valid) {
        o.frame = nullptr; o.valid = false;
    }
};

class AudioStreamDecoder {
public:
    AudioStreamDecoder() = default;
    ~AudioStreamDecoder() { close(); }

    AudioStreamDecoder(const AudioStreamDecoder &) = delete;
    AudioStreamDecoder &operator=(const AudioStreamDecoder &) = delete;

    bool open(AVFormatContext *fmtCtx, int streamIdx) {
        close();

        AVStream *st = fmtCtx->streams[streamIdx];
        if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
            return false;

        const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!codec) return false;

        m_codecCtx = avcodec_alloc_context3(codec);
        if (!m_codecCtx) return false;

        if (avcodec_parameters_to_context(m_codecCtx, st->codecpar) < 0) {
            avcodec_free_context(&m_codecCtx);
            return false;
        }

        if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
            avcodec_free_context(&m_codecCtx);
            return false;
        }

        m_streamIdx = streamIdx;
        m_timeBase = av_q2d(st->time_base);

        return true;
    }

    void close() {
        if (m_codecCtx) {
            avcodec_free_context(&m_codecCtx);
            m_codecCtx = nullptr;
        }
        m_streamIdx = -1;
    }

    bool isOpen() const { return m_codecCtx != nullptr; }

    bool sendPacket(AVPacket *pkt) {
        if (!m_codecCtx) return false;
        int ret = avcodec_send_packet(m_codecCtx, pkt);
        return ret >= 0;
    }

    bool receiveFrame(DecodedAudioFrame &out) {
        if (!m_codecCtx || !out.frame) return false;
        av_frame_unref(out.frame);

        int ret = avcodec_receive_frame(m_codecCtx, out.frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            out.valid = false;
            return false;
        }
        if (ret < 0) {
            out.valid = false;
            return false;
        }

        out.ptsSec = (out.frame->pts != AV_NOPTS_VALUE)
            ? static_cast<double>(out.frame->pts) * m_timeBase
            : 0.0;
        out.valid = true;
        return true;
    }

    void flush() {
        if (m_codecCtx) avcodec_flush_buffers(m_codecCtx);
    }

    AVCodecContext *codecContext() const { return m_codecCtx; }
    int streamIndex() const { return m_streamIdx; }

private:
    AVCodecContext *m_codecCtx = nullptr;
    int m_streamIdx = -1;
    double m_timeBase = 1.0;
};

#endif
