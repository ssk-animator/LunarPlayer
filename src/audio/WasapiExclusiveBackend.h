#ifndef WASAPIEXCLUSIVEBACKEND_H
#define WASAPIEXCLUSIVEBACKEND_H

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include "IAudioBackend.h"
#include <cstdint>
#include <atomic>
#include <mutex>

class WasapiExclusiveBackend : public IAudioBackend {
public:
    WasapiExclusiveBackend();
    ~WasapiExclusiveBackend() override;

    AudioBackendType type() const override { return AudioBackendType::WasapiExclusive; }
    std::string name() const override { return "WASAPI Exclusive"; }

    bool open(const AudioFormat &format) override;
    void close() override;
    bool isOpen() const override { return m_opened; }

    int write(const float *data, int frames) override;
    double currentPositionSec() const override;
    double getLatencySec() const override;

    void setVolume(float vol) override;
    float volume() const override { return m_volume; }

    void reset() override;

    AudioBackendStats stats() const override;

    int preferredBufferFrames() const override { return 4800; }
    int minBufferFrames() const override { return 1024; }
    int maxBufferFrames() const override { return 32768; }

    AudioFormat negotiatedFormat() const override { return m_format; }

    bool deviceSupportsPassthrough(BitstreamCodec codec) const override;
    bool openPassthrough(BitstreamCodec codec, int sampleRate, int bitrate) override;
    int writePassthrough(const uint8_t *data, int size) override;

private:
    bool initExclusive();

    IMMDeviceEnumerator *m_enumerator = nullptr;
    IMMDevice *m_device = nullptr;
    IAudioClient *m_audioClient = nullptr;
    IAudioRenderClient *m_renderClient = nullptr;
    IAudioClock *m_audioClock = nullptr;
    HANDLE m_eventHandle = nullptr;

    AudioFormat m_format;
    float m_volume = 1.0f;
    bool m_opened = false;

    int m_bufferFrameCount = 0;
    int m_bytesPerFrame = 0;

    mutable std::mutex m_writeMutex;
    std::atomic<int64_t> m_totalFramesWritten{0};
    int m_underruns = 0;
    int m_overruns = 0;

    BitstreamCodec m_passthroughCodec = BitstreamCodec::None;
    bool m_passthroughMode = false;
};

#endif
