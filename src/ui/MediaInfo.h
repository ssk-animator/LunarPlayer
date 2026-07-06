#ifndef MEDIAINFO_H
#define MEDIAINFO_H

#include <QString>

struct MediaInfo
{
    QString fileName;

    // Container
    QString containerFormat;
    QString containerLongName;

    // Video codec
    QString codecName;
    QString codecLongName;
    QString profile;
    QString level;

    int width = 0;
    int height = 0;

    double fps = 0.0;
    double durationSec = 0.0;

    QString pixelFormat;
    int bitDepth = 0;
    QString chromaSubsampling;
    QString colorSpace;
    QString hdrType;

    int64_t bitrate = 0;

    QString decoder;
    QString decoderPath;
    QString renderer;

    // Audio stream
    QString audioFormat;
    QString audioCodecLongName;
    int audioSampleRate = 0;
    int audioChannels = 0;
    QString audioChannelLayout;
    QString audioSampleFormat;
    int audioBitDepth = 0;
    int64_t audioBitrate = 0;
    QString audioProfile;
    int audioFrameSize = 0;
    int audioBlockAlign = 0;
    QString audioCompressionRatio;

    // Audio output
    QString audioBackend;
    QString audioOutputDevice;
    QString audioOutputFormat;
    int audioOutputSampleRate = 0;
    int audioOutputChannels = 0;
    double audioOutputLatencyMs = 0.0;
    double audioClockDriftMs = 0.0;

    // Device info
    QString deviceName;
    QString deviceManufacturer;
    int deviceMaxChannels = 0;
    int deviceMinSampleRate = 0;
    int deviceMaxSampleRate = 0;
    bool deviceSupportsExclusive = false;
    bool deviceSupportsPassthroughAC3 = false;
    bool deviceSupportsPassthroughEAC3 = false;
    bool deviceSupportsPassthroughDTS = false;
    QString channelOperation;

    // Audio runtime stats
    int64_t audioPacketsDecoded = 0;
    int64_t audioFramesResampled = 0;
    int64_t audioFramesWritten = 0;
    int audioUnderruns = 0;
    int audioOverruns = 0;
    double audioDecodeMs = 0.0;
    double audioResampleMs = 0.0;

    // Streams
    int videoStreams = 0;
    int audioStreams = 0;
    int subtitleStreams = 0;
    int otherStreams = 0;

    // Metadata
    QString creationTime;

    // Image sequence info
    bool isImageSequence = false;
    QString sequencePattern;
    int sequenceFrameCount = 0;

    // Playback diagnostics
    int64_t totalFramesDecoded = 0;
    int packetsSkipped = 0;
    int framesDropped = 0;
    int seeksCount = 0;
    double seekLatencyMs = 0.0;
    double avgDecodeMs = 0.0;
    double peakDecodeMs = 0.0;
};

#endif
