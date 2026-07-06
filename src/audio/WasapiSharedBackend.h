#ifndef WASAPISHAREDBACKEND_H
#define WASAPISHAREDBACKEND_H

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include "IAudioBackend.h"
#include "AudioRingBuffer.h"
#include <cstdint>
#include <atomic>
#include <memory>

class WasapiSharedBackend : public IAudioBackend {
public:
    WasapiSharedBackend();
    ~WasapiSharedBackend() override;

    AudioBackendType type() const override { return AudioBackendType::WasapiShared; }
    std::string name() const override { return "WASAPI Shared"; }

    bool open(const AudioFormat &format) override;
    void close() override;
    bool isOpen() const override { return m_opened; }
    void startPlayback() override;

    int deviceMaxChannels() const override { return m_deviceChannels; }
    int deviceSampleRate() const override { return m_deviceSampleRate; }
    bool deviceSupportsExclusive() const override { return true; }

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

    void setRingBuffer(AudioRingBuffer *buf) { m_ringBuf = buf; }

private:
    IMMDeviceEnumerator *m_enumerator = nullptr;
    IMMDevice *m_device = nullptr;
    IAudioClient *m_audioClient = nullptr;
    IAudioRenderClient *m_renderClient = nullptr;
    IAudioClock *m_audioClock = nullptr;
    HANDLE m_threadHandle = nullptr;
    bool m_threadRunning = false;

    AudioFormat m_format;
    float m_volume = 1.0f;
    bool m_opened = false;
    bool m_usesPcm16 = false;
    int m_deviceBitsPerSample = 32;
    int m_deviceChannels = 2;
    int m_deviceSampleRate = 48000;

    int m_bufferFrameCount = 0;
    int m_bytesPerFrame = 0;

    AudioRingBuffer *m_ringBuf = nullptr;

    std::atomic<int64_t> m_totalFramesWritten{0};
    int m_underruns = 0;
    int m_overruns = 0;

    static DWORD WINAPI renderThreadProc(LPVOID param);
};

#endif
