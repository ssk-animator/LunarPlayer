#ifndef AUDIORINGBUFFER_H
#define AUDIORINGBUFFER_H

#include <cstdint>
#include <cstring>
#include <atomic>
#include <algorithm>

class AudioRingBuffer {
public:
    explicit AudioRingBuffer(int capacityFrames, int channels)
        : m_capacity(capacityFrames)
        , m_channels(channels)
    {
        m_buffer = new float[static_cast<size_t>(capacityFrames) * channels];
        std::memset(m_buffer, 0, static_cast<size_t>(capacityFrames) * channels * sizeof(float));
    }

    ~AudioRingBuffer() { delete[] m_buffer; }

    AudioRingBuffer(const AudioRingBuffer &) = delete;
    AudioRingBuffer &operator=(const AudioRingBuffer &) = delete;

    int write(const float *data, int frames) {
        int avail = m_capacity - m_framesLoaded.load(std::memory_order_relaxed);
        int toWrite = std::min(frames, avail);
        if (toWrite <= 0) {
            m_overruns++;
            return 0;
        }

        int ch = m_channels;
        int writePos = m_writePos.load(std::memory_order_relaxed);

        // First chunk (may wrap)
        int first = std::min(toWrite, m_capacity - writePos);
        std::memcpy(m_buffer + writePos * ch, data, static_cast<size_t>(first) * ch * sizeof(float));
        if (first < toWrite) {
            std::memcpy(m_buffer, data + first * ch,
                        static_cast<size_t>(toWrite - first) * ch * sizeof(float));
        }

        m_writePos.store((writePos + toWrite) % m_capacity, std::memory_order_release);
        m_framesLoaded.fetch_add(toWrite, std::memory_order_release);
        return toWrite;
    }

    int read(float *data, int frames) {
        int avail = m_framesLoaded.load(std::memory_order_relaxed);
        int toRead = std::min(frames, avail);
        if (toRead <= 0) {
            m_underruns++;
            return 0;
        }

        int ch = m_channels;
        int readPos = m_readPos.load(std::memory_order_relaxed);

        int first = std::min(toRead, m_capacity - readPos);
        std::memcpy(data, m_buffer + readPos * ch, static_cast<size_t>(first) * ch * sizeof(float));
        if (first < toRead) {
            std::memcpy(data + first * ch, m_buffer,
                        static_cast<size_t>(toRead - first) * ch * sizeof(float));
        }

        m_readPos.store((readPos + toRead) % m_capacity, std::memory_order_release);
        m_framesLoaded.fetch_sub(toRead, std::memory_order_release);
        return toRead;
    }

    int availableRead() const { return m_framesLoaded.load(std::memory_order_acquire); }
    int availableWrite() const { return m_capacity - m_framesLoaded.load(std::memory_order_acquire); }
    int capacity() const { return m_capacity; }
    int channels() const { return m_channels; }

    void reset() {
        m_writePos.store(0, std::memory_order_relaxed);
        m_readPos.store(0, std::memory_order_relaxed);
        m_framesLoaded.store(0, std::memory_order_relaxed);
    }

    int underruns() const { return m_underruns; }
    int overruns() const { return m_overruns; }

private:
    float *m_buffer;
    int m_capacity;
    int m_channels;

    std::atomic<int> m_writePos{0};
    std::atomic<int> m_readPos{0};
    std::atomic<int> m_framesLoaded{0};

    std::atomic<int> m_underruns{0};
    std::atomic<int> m_overruns{0};
};

#endif
