#ifndef WAVEOUTBACKEND_H
#define WAVEOUTBACKEND_H

#include "IAudioBackend.h"
#include <cstdint>
#include <atomic>

class WaveOutBackend : public IAudioBackend {
public:
    WaveOutBackend();
    ~WaveOutBackend() override;

    AudioBackendType type() const override { return AudioBackendType::WaveOut; }
    std::string name() const override { return "waveOut (legacy)"; }

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

    int preferredBufferFrames() const override { return 4096; }
    int minBufferFrames() const override { return 1024; }
    int maxBufferFrames() const override { return 16384; }

    AudioFormat negotiatedFormat() const override { return m_format; }

    void onBufferDone(int idx);

private:
    static const int NUM_BUFFERS = 24;
    static const int BUFFER_FRAMES = 2048;

    struct Buffer {
        short *data = nullptr;
        std::atomic<bool> active{false};
        std::atomic<bool> playing{false};
        std::atomic<bool> prepared{false};
    };

    Buffer m_buffers[NUM_BUFFERS];
    int m_writeBuffer = 0;

    AudioFormat m_format;
    float m_volume = 1.0f;

    float *m_accum = nullptr;
    int m_accumFrames = 0;

    void *m_hWaveOut = nullptr;
    void *m_headers[NUM_BUFFERS];
    int64_t m_totalFramesWritten = 0;
    bool m_opened = false;

    // Stats
    int m_underruns = 0;
    int m_overruns = 0;

    void submitBuffer(int idx);
};

#endif
