#ifndef HWACCEL_H
#define HWACCEL_H

#include <QString>
#include <vector>
#include "DecoderInfo.h"

extern "C" {
#include <libavutil/hwcontext.h>
}

struct GPUInfo
{
    uint32_t vendorId = 0;
    QString name;
};

GPUInfo detectGPU();
std::vector<AVHWDeviceType> probeOrder(const GPUInfo &gpu);
QString backendName(DecodeBackend b);
DecodeBackend typeToBackend(AVHWDeviceType type);

#endif
