#ifndef HWACCEL_H
#define HWACCEL_H

#include <QString>
#include <vector>
#include "DecoderInfo.h"

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavcodec/avcodec.h>
}

GPUInfo detectGPU();
QString vendorName(uint32_t vendorId);
bool isNVIDIA(uint32_t vendorId);
bool isIntel(uint32_t vendorId);
bool isAMD(uint32_t vendorId);
std::vector<AVHWDeviceType> probeOrder(const GPUInfo &gpu);
QString backendName(DecodeBackend b);
DecodeBackend typeToBackend(AVHWDeviceType type);

DecoderScore scoreDecoder(const GPUInfo &gpu, AVHWDeviceType type,
                          AVCodecID codecId, int width, int height, int64_t bitrate);

bool isCodecSupportedHW(AVCodecID codecId, AVHWDeviceType hwType);

const char* hwDecoderName(AVCodecID codecId, AVHWDeviceType hwType);

#endif
