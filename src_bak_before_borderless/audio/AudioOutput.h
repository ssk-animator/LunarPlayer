#ifndef AUDIOOUTPUT_H
#define AUDIOOUTPUT_H

#include <cstdint>

class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();

    bool open(int sampleRate, int channels);
    void close();
    bool isOpen() const;

    int write(const float *data, int frames);
    double currentPositionSec() const;
    int64_t writtenFrames() const { return m_totalFramesWritten; }

    void setVolume(float vol);
    float volume() const;
    void reset();

private:
    static const int NUM_BUFFERS = 4;
    static const int BUFFER_FRAMES = 4096;

    struct Buffer {
        short *data = nullptr;
        bool active = false;
        bool playing = false;
        bool prepared = false;
    };

    Buffer m_buffers[NUM_BUFFERS];
    int m_writeBuffer = 0;

    int m_sampleRate = 48000;
    int m_channels = 2;
    float m_volume = 1.0f;

    float m_accum[BUFFER_FRAMES * 2];
    int m_accumFrames = 0;

    void* m_hWaveOut = nullptr;
    void* m_headers[NUM_BUFFERS];
    int64_t m_totalFramesWritten = 0;
    bool m_opened = false;

    void submitBuffer(int idx);

public:
    void onBufferDone(int idx);
};

#endif
