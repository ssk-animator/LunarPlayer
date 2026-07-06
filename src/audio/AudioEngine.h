#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

#include "IAudioBackend.h"
#include "AudioStreamDecoder.h"
#include "AudioResampler.h"
#include "AudioClock.h"
#include "AudioRingBuffer.h"
#include "AudioBackendFactory.h"
#include "AudioChannelRouter.h"
#include "AudioDSP.h"
#include <QObject>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
}

#include <cstdint>
#include <atomic>
#include <queue>
#include <mutex>
#include <memory>
#include <functional>
#include <thread>

struct AudioPipelineStats {
    int64_t packetsDecoded = 0;
    int64_t framesDecoded = 0;
    int decodeErrors = 0;
    int64_t framesResampled = 0;
    int64_t framesWritten = 0;
    int underruns = 0;
    int overruns = 0;
    double clockPositionSec = 0.0;
    double devicePositionSec = 0.0;
    double latencyMs = 0.0;
    double decodeMs = 0.0;
    double resampleMs = 0.0;
    std::string codecName;
    std::string sampleFormat;
    std::string channelLayout;
    int sampleRate = 0;
    int outputChannels = 0;
    std::string outputFormat;
    std::string backendName;
};

class AudioEngine : public QObject {
    Q_OBJECT

public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine();

    bool open(AVFormatContext *fmtCtx, int audioStreamIdx,
              AudioBackendType backendType = AudioBackendType::WasapiShared);
    void close();
    bool isOpen() const { return m_isOpen; }

    using PacketSource = std::function<bool(AVPacket **)>;
    void setPacketSource(PacketSource src) { m_packetSource = src; }

    void start();
    void stop();
    void pause();
    void resume();
    void reset();

    void flush();

    double clockPositionSec() const;
    void syncToPts(double ptsSec);

    void setVolume(float vol);
    float volume() const;

    AudioPipelineStats stats() const;

    AudioBackendType backendType() const { return m_backend ? m_backend->type() : AudioBackendType::WaveOut; }
    std::string backendName() const { return m_backend ? m_backend->name() : "none"; }
    int sampleRate() const { return m_outputFormat.sampleRate; }
    int channels() const { return m_outputFormat.channels; }

    AudioDSPChain &dspChain() { return m_dspChain; }
    const AudioDSPChain &dspChain() const { return m_dspChain; }
    ChannelOperation channelOperation() const { return m_channelOperation; }
    BitstreamCodec passthroughCodec() const { return m_passthroughCodec; }
    bool isPassthrough() const { return m_passthroughCodec != BitstreamCodec::None; }

signals:
    void clockUpdated(double positionSec);
    void underrun();
    void error(const QString &message);

private:
    bool openWithFallback(AVFormatContext *fmtCtx, int streamIdx, AudioBackendType preferred);
    void decodeAndProcess();
    void processDecodedFrame(DecodedAudioFrame &df);
    void audioFeedLoop();

    std::unique_ptr<IAudioBackend> m_backend;
    AudioStreamDecoder m_decoder;
    AudioResampler m_resampler;
    AudioClock m_clock;
    std::unique_ptr<AudioRingBuffer> m_ringBuffer;
    AudioDSPChain m_dspChain;

    AVFormatContext *m_fmtCtx = nullptr;

    AudioFormat m_inputFormat;
    AudioFormat m_outputFormat;
    ChannelOperation m_channelOperation = ChannelOperation::Native;
    BitstreamCodec m_passthroughCodec = BitstreamCodec::None;

    PacketSource m_packetSource;

    std::thread m_audioThread;
    std::atomic<bool> m_threadRunning{false};
    static const int FEED_INTERVAL_US = 3000; // 3ms between audio feed iterations

    std::atomic<bool> m_isOpen{false};
    std::atomic<bool> m_isPlaying{false};
    std::atomic<bool> m_isPaused{false};

    std::queue<AVPacket *> m_packetQueue;
    std::mutex m_packetMutex;

    float *m_convertBuffer = nullptr;
    int m_convertBufferCapacity = 0;

    int64_t m_totalPacketsDecoded = 0;
    int64_t m_totalFramesDecoded = 0;
    int m_totalDecodeErrors = 0;
    int64_t m_totalFramesResampled = 0;
    int64_t m_totalFramesWritten = 0;

    std::atomic<double> m_clockBasePts{0.0};
    std::atomic<double> m_clockBaseTime{0.0};

    // Instrumentation
    std::atomic<double> m_lastDecodeMs{0.0};
    std::atomic<double> m_lastResampleMs{0.0};
    std::atomic<double> m_lastWriteMs{0.0};
    std::atomic<double> m_lastRingFillPct{0.0};
    std::atomic<double> m_lastWakeJitterMs{0.0};
    std::atomic<int> m_iterCount{0};
};

#endif
