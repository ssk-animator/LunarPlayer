#ifndef IAUDIOBACKEND_H
#define IAUDIOBACKEND_H

#include <cstdint>
#include <string>
#include <vector>

enum class AudioBackendType {
    WaveOut,
    WasapiShared,
    WasapiExclusive,
    Asio
};

enum class AudioSampleFormat {
    Unknown,
    Int16,
    Int16Planar,
    Int32,
    Int32Planar,
    Float32,
    Float32Planar,
    Float64
};

enum class BitstreamCodec {
    None,
    AC3,
    EAC3,
    DTS,
    DTS_HD,
    TrueHD
};

struct AudioFormat {
    int sampleRate = 48000;
    int channels = 2;
    AudioSampleFormat sampleFormat = AudioSampleFormat::Float32;
    uint64_t channelLayout = 0;
};

struct AudioDeviceInfo {
    std::wstring id;
    std::wstring name;
    std::wstring description;
    bool isDefault = false;
    bool isInput = false;
};

struct AudioBackendStats {
    int64_t totalFramesWritten = 0;
    int64_t totalFramesRead = 0;
    int bufferUnderruns = 0;
    int bufferOverruns = 0;
    double averageLatencyMs = 0.0;
    double peakLatencyMs = 0.0;
    int activeBufferCount = 0;
    int totalBufferCount = 0;
};

class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;

    virtual AudioBackendType type() const = 0;
    virtual std::string name() const = 0;

    virtual bool open(const AudioFormat &format) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual void startPlayback() {}

    virtual int deviceMaxChannels() const { return 2; }
    virtual int deviceSampleRate() const { return 48000; }
    virtual bool deviceSupportsExclusive() const { return false; }

    virtual bool deviceSupportsPassthrough(BitstreamCodec codec) const { return false; }
    virtual bool openPassthrough(BitstreamCodec codec, int sampleRate, int bitrate) { return false; }
    virtual int writePassthrough(const uint8_t *data, int size) { return 0; }

    virtual int write(const float *data, int frames) = 0;
    virtual double currentPositionSec() const = 0;
    virtual double getLatencySec() const = 0;

    virtual void setVolume(float vol) = 0;
    virtual float volume() const = 0;

    virtual void reset() = 0;

    virtual AudioBackendStats stats() const = 0;

    virtual int preferredBufferFrames() const = 0;
    virtual int minBufferFrames() const = 0;
    virtual int maxBufferFrames() const = 0;

    virtual AudioFormat negotiatedFormat() const = 0;
};

#endif
