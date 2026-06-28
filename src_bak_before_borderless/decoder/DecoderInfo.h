#ifndef DECODERINFO_H
#define DECODERINFO_H

#include <QString>
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

struct DecoderInfo
{
    DecodeBackend backend = DecodeBackend::Software;
    QString decoderName;
    QString gpuName;
    AVHWDeviceType hwType = AV_HWDEVICE_TYPE_NONE;
    bool hardwareAccelerated = false;
};

#endif
