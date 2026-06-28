#ifndef MEDIAINFO_H
#define MEDIAINFO_H

#include <QString>

struct MediaInfo
{
    QString fileName;

    QString codecName;
    QString codecLongName;
    QString profile;

    int width = 0;
    int height = 0;

    double fps = 0.0;
    double durationSec = 0.0;

    QString pixelFormat;
    QString colorSpace;
    QString hdrType;

    int64_t bitrate = 0;

    QString decoder;
    QString renderer;

    QString audioFormat;
    int audioSampleRate = 0;
    int audioChannels = 0;

    int videoStreams = 0;
    int audioStreams = 0;
    int subtitleStreams = 0;

    // Image sequence info
    bool isImageSequence = false;
    QString sequencePattern;
    int sequenceFrameCount = 0;
};

#endif
