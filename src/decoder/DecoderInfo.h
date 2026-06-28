#ifndef DECODERINFO_H
#define DECODERINFO_H

#include <QString>
#include <QVector>
extern "C" {
#include <libavutil/hwcontext.h>
}

enum class DecodeBackend
{
    Software,
    D3D11VA,
    NVDEC,
    QuickSync,
    AMF
};

struct GPUInfo
{
    uint32_t vendorId = 0;
    QString name;
    QString driverVersion;
    uint64_t dedicatedVideoMemory = 0;
    uint64_t dedicatedSystemMemory = 0;
    uint64_t sharedSystemMemory = 0;
    bool isHardwareDecoderAvailable = false;
};

struct DecoderScore
{
    int totalScore = 0;
    int gpuSupported = 0;
    int codecSupported = 0;
    int driverAvailable = 0;
    int resolutionBonus = 0;
    int bitrateBonus = 0;
    int stabilityBonus = 0;
    int initPenalty = 0;
    QStringList reasons;
};

struct DecoderInfo
{
    DecodeBackend backend = DecodeBackend::Software;
    QString decoderName;
    QString gpuName;
    QString driverVersion;
    AVHWDeviceType hwType = AV_HWDEVICE_TYPE_NONE;
    bool hardwareAccelerated = false;
    DecoderScore score;
    double initTimeMs = 0.0;
    int decodeFps = 0;
    double decodeLatencyMs = 0.0;
    int droppedFrames = 0;
    bool unstable = false;
    int fallbackCount = 0;
    QString lastError;
};

#endif
