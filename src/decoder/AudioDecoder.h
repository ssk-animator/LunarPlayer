#ifndef AUDIODECODER_H
#define AUDIODECODER_H

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
}

class AudioDecoder {
public:
    AudioDecoder();
    ~AudioDecoder();

    bool open(AVFormatContext *fmtCtx, int streamIdx);
    void close();
    bool isOpen() const;

    int decode(const AVPacket *pkt, float *outBuffer, int maxFrames);
    void flush();

    int sampleRate() const { return m_outputSampleRate; }
    int channels() const { return m_outputChannels; }
    int streamIndex() const { return m_streamIdx; }

private:
    AVCodecContext *m_codecCtx = nullptr;
    const AVCodec *m_codec = nullptr;
    SwrContext *m_swrCtx = nullptr;
    AVFrame *m_frame = nullptr;

    int m_streamIdx = -1;
    int m_outputSampleRate = 48000;
    int m_outputChannels = 2;
};

#endif
