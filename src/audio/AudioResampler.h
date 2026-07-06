#ifndef AUDIORESAMPLER_H
#define AUDIORESAMPLER_H

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include "IAudioBackend.h"
#include <cstdint>

class AudioResampler {
public:
    AudioResampler() = default;
    ~AudioResampler() { close(); }

    AudioResampler(const AudioResampler &) = delete;
    AudioResampler &operator=(const AudioResampler &) = delete;

    bool open(const AudioFormat &inputFormat, const AudioFormat &outputFormat) {
        close();

        AVChannelLayout inLayout = {};
        av_channel_layout_from_mask(&inLayout, inputFormat.channelLayout);

        AVChannelLayout outLayout = {};
        av_channel_layout_from_mask(&outLayout, outputFormat.channelLayout);

        AVSampleFormat inFmt = toAVSampleFormat(inputFormat.sampleFormat);
        AVSampleFormat outFmt = toAVSampleFormat(outputFormat.sampleFormat);

        int ret = swr_alloc_set_opts2(&m_swrCtx,
            &outLayout, outFmt, outputFormat.sampleRate,
            &inLayout, inFmt, inputFormat.sampleRate,
            0, nullptr);

        av_channel_layout_uninit(&inLayout);
        av_channel_layout_uninit(&outLayout);

        if (ret < 0 || !m_swrCtx) return false;
        if (swr_init(m_swrCtx) < 0) {
            swr_free(&m_swrCtx);
            return false;
        }

        m_inputFormat = inputFormat;
        m_outputFormat = outputFormat;
        return true;
    }

    void close() {
        if (m_swrCtx) {
            swr_free(&m_swrCtx);
            m_swrCtx = nullptr;
        }
    }

    bool isOpen() const { return m_swrCtx != nullptr; }

    int convert(const AVFrame *frame, float **outData, int outFrames) {
        if (!m_swrCtx || !frame) return 0;
        return swr_convert(m_swrCtx, reinterpret_cast<uint8_t **>(outData), outFrames,
                           const_cast<const uint8_t **>(frame->data), frame->nb_samples);
    }

    int outputSampleRate() const { return m_outputFormat.sampleRate; }
    int outputChannels() const { return m_outputFormat.channels; }
    const AudioFormat &inputFormat() const { return m_inputFormat; }

    bool reconfigureInputFormat(AudioSampleFormat newInputFormat) {
        if (!m_swrCtx) return false;
        swr_free(&m_swrCtx);

        AVChannelLayout inLayout = {};
        av_channel_layout_from_mask(&inLayout, m_inputFormat.channelLayout);

        AVChannelLayout outLayout = {};
        av_channel_layout_from_mask(&outLayout, m_outputFormat.channelLayout);

        AVSampleFormat inFmt = toAVSampleFormat(newInputFormat);
        AVSampleFormat outFmt = toAVSampleFormat(m_outputFormat.sampleFormat);

        int ret = swr_alloc_set_opts2(&m_swrCtx,
            &outLayout, outFmt, m_outputFormat.sampleRate,
            &inLayout, inFmt, m_inputFormat.sampleRate,
            0, nullptr);

        av_channel_layout_uninit(&inLayout);
        av_channel_layout_uninit(&outLayout);

        if (ret < 0 || !m_swrCtx) return false;
        if (swr_init(m_swrCtx) < 0) {
            swr_free(&m_swrCtx);
            return false;
        }

        m_inputFormat.sampleFormat = newInputFormat;
        return true;
    }

private:
    static AVSampleFormat toAVSampleFormat(AudioSampleFormat fmt) {
        switch (fmt) {
        case AudioSampleFormat::Int16:  return AV_SAMPLE_FMT_S16;
        case AudioSampleFormat::Int16Planar:  return AV_SAMPLE_FMT_S16P;
        case AudioSampleFormat::Int32:  return AV_SAMPLE_FMT_S32;
        case AudioSampleFormat::Int32Planar:  return AV_SAMPLE_FMT_S32P;
        case AudioSampleFormat::Float32: return AV_SAMPLE_FMT_FLT;
        case AudioSampleFormat::Float32Planar: return AV_SAMPLE_FMT_FLTP;
        case AudioSampleFormat::Float64: return AV_SAMPLE_FMT_DBL;
        default: return AV_SAMPLE_FMT_FLT;
        }
    }

    SwrContext *m_swrCtx = nullptr;
    AudioFormat m_inputFormat;
    AudioFormat m_outputFormat;
};

#endif
