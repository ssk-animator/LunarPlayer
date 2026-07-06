#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_2.h>
#include <versionhelpers.h>

#include "HWAccel.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QByteArray>
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>

GPUInfo detectGPU()
{
    GPUInfo info;

    IDXGIFactory1 *factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) {
        qWarning() << "HWAccel: CreateDXGIFactory1 failed" << hr;
        return info;
    }

    IDXGIAdapter1 *adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
        DXGI_ADAPTER_DESC1 desc;
        if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
            desc.VendorId != 0 &&
            desc.VendorId != 0x1414)
        {
            info.vendorId = desc.VendorId;
            info.name = QString::fromWCharArray(desc.Description);
            info.dedicatedVideoMemory = desc.DedicatedVideoMemory;
            info.dedicatedSystemMemory = desc.DedicatedSystemMemory;
            info.sharedSystemMemory = desc.SharedSystemMemory;
            adapter->Release();
            break;
        }
        adapter->Release();
    }

    factory->Release();

    qDebug() << "HWAccel: detected GPU" << info.name
             << "vendorId = 0x" + QString::number(info.vendorId, 16);

    return info;
}

QString vendorName(uint32_t vendorId)
{
    switch (vendorId) {
    case 0x10DE: return "NVIDIA";
    case 0x8086: return "Intel";
    case 0x1002: return "AMD";
    case 0x13A3: return "HiSilicon";
    case 0x1AE0: return "Google";
    default:     return "Unknown";
    }
}

bool isNVIDIA(uint32_t vendorId) { return vendorId == 0x10DE; }
bool isIntel(uint32_t vendorId)  { return vendorId == 0x8086; }
bool isAMD(uint32_t vendorId)    { return vendorId == 0x1002; }

std::vector<AVHWDeviceType> probeOrder(const GPUInfo &gpu)
{
    switch (gpu.vendorId) {
    case 0x10DE:
        return {AV_HWDEVICE_TYPE_CUDA, AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_DXVA2};
    case 0x8086:
        return {AV_HWDEVICE_TYPE_QSV, AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_DXVA2};
    case 0x1002:
        return {AV_HWDEVICE_TYPE_AMF, AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_DXVA2};
    default:
        return {AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_DXVA2};
    }
}

QString backendName(DecodeBackend b)
{
    switch (b) {
    case DecodeBackend::NVDEC:     return "NVDEC";
    case DecodeBackend::QuickSync: return "QuickSync";
    case DecodeBackend::AMF:       return "AMF";
    case DecodeBackend::D3D11VA:   return "D3D11VA";
    default:                       return "Software";
    }
}

DecodeBackend typeToBackend(AVHWDeviceType type)
{
    switch (type) {
    case AV_HWDEVICE_TYPE_CUDA:    return DecodeBackend::NVDEC;
    case AV_HWDEVICE_TYPE_QSV:     return DecodeBackend::QuickSync;
    case AV_HWDEVICE_TYPE_AMF:     return DecodeBackend::AMF;
    case AV_HWDEVICE_TYPE_D3D11VA: return DecodeBackend::D3D11VA;
    default:                       return DecodeBackend::Software;
    }
}

bool isCodecSupportedHW(AVCodecID codecId, AVHWDeviceType hwType)
{
    if (hwType == AV_HWDEVICE_TYPE_NONE)
        return false;

    switch (codecId) {
    case AV_CODEC_ID_H264:
        return true;
    case AV_CODEC_ID_HEVC:
        return true;
    case AV_CODEC_ID_VP9:
        return hwType == AV_HWDEVICE_TYPE_D3D11VA ||
               hwType == AV_HWDEVICE_TYPE_CUDA;
    case AV_CODEC_ID_AV1:
        return hwType == AV_HWDEVICE_TYPE_D3D11VA ||
               hwType == AV_HWDEVICE_TYPE_CUDA;
    case AV_CODEC_ID_MPEG2VIDEO:
        return hwType == AV_HWDEVICE_TYPE_D3D11VA ||
               hwType == AV_HWDEVICE_TYPE_DXVA2 ||
               hwType == AV_HWDEVICE_TYPE_CUDA;
    case AV_CODEC_ID_VC1:
        return hwType == AV_HWDEVICE_TYPE_D3D11VA ||
               hwType == AV_HWDEVICE_TYPE_DXVA2 ||
               hwType == AV_HWDEVICE_TYPE_CUDA;
    default:
        return false;
    }
}

const char* hwDecoderName(AVCodecID codecId, AVHWDeviceType hwType)
{
    switch (codecId) {
    case AV_CODEC_ID_H264:
        switch (hwType) {
        case AV_HWDEVICE_TYPE_D3D11VA: return "h264_d3d11va";
        case AV_HWDEVICE_TYPE_DXVA2:   return "h264_dxva2";
        case AV_HWDEVICE_TYPE_CUDA:    return "h264_cuviddec";
        case AV_HWDEVICE_TYPE_QSV:     return "h264_qsv";
        case AV_HWDEVICE_TYPE_AMF:     return "h264_amf";
        default: return nullptr;
        }
    case AV_CODEC_ID_HEVC:
        switch (hwType) {
        case AV_HWDEVICE_TYPE_D3D11VA: return "hevc_d3d11va2";
        case AV_HWDEVICE_TYPE_DXVA2:   return "hevc_dxva2";
        case AV_HWDEVICE_TYPE_CUDA:    return "hevc_cuvid";
        case AV_HWDEVICE_TYPE_QSV:     return "hevc_qsv";
        case AV_HWDEVICE_TYPE_AMF:     return "hevc_amf";
        default: return nullptr;
        }
    case AV_CODEC_ID_VP9:
        switch (hwType) {
        case AV_HWDEVICE_TYPE_D3D11VA: return "vp9_d3d11va";
        case AV_HWDEVICE_TYPE_CUDA:    return "vp9_cuviddec";
        default: return nullptr;
        }
    case AV_CODEC_ID_AV1:
        switch (hwType) {
        case AV_HWDEVICE_TYPE_D3D11VA: return "av1_d3d11va";
        case AV_HWDEVICE_TYPE_CUDA:    return "av1_cuviddec";
        default: return nullptr;
        }
    case AV_CODEC_ID_MPEG2VIDEO:
        switch (hwType) {
        case AV_HWDEVICE_TYPE_D3D11VA: return "mpeg2_d3d11va";
        case AV_HWDEVICE_TYPE_DXVA2:   return "mpeg2_dxva2";
        case AV_HWDEVICE_TYPE_CUDA:    return "mpeg2_cuviddec";
        default: return nullptr;
        }
    case AV_CODEC_ID_VC1:
        switch (hwType) {
        case AV_HWDEVICE_TYPE_D3D11VA: return "vc1_d3d11va";
        case AV_HWDEVICE_TYPE_DXVA2:   return "vc1_dxva2";
        case AV_HWDEVICE_TYPE_CUDA:    return "vc1_cuviddec";
        default: return nullptr;
        }
    default:
        return nullptr;
    }
}

DecoderScore scoreDecoder(const GPUInfo &gpu, AVHWDeviceType type,
                          AVCodecID codecId, int width, int height, int64_t bitrate)
{
    DecoderScore s;
    QString typeName = QString::number(type);

    if (!isCodecSupportedHW(codecId, type)) {
        return s;
    }

    s.gpuSupported = 25;
    s.codecSupported = 25;

    const char *name = hwDecoderName(codecId, type);
    if (!name) {
        return s;
    }

    const AVCodec *hwCodec = avcodec_find_decoder_by_name(name);
    if (!hwCodec) {
        return s;
    }

    AVBufferRef *hwCtx = nullptr;
    int devRet = av_hwdevice_ctx_create(&hwCtx, type, nullptr, nullptr, 0);
    if (devRet < 0) {
        return s;
    }

    AVCodecContext *testCtx = avcodec_alloc_context3(hwCodec);
    if (!testCtx) {
        av_buffer_unref(&hwCtx);
        return s;
    }

    testCtx->width = width;
    testCtx->height = height;
    testCtx->bit_rate = bitrate > 0 ? bitrate : 10000000;
    testCtx->hw_device_ctx = av_buffer_ref(hwCtx);

    int ret = avcodec_open2(testCtx, hwCodec, nullptr);
    if (ret < 0) {
        char errBuf[256];
        av_strerror(ret, errBuf, sizeof(errBuf));
        s.initPenalty = -30;
        s.reasons << QString("Init failed: %1").arg(errBuf);
    } else {
        s.driverAvailable = 25;
        s.stabilityBonus = 10;

        if (width * height > 1920 * 1080)
            s.resolutionBonus = 10;
        if (width * height > 3840 * 2160)
            s.resolutionBonus = 20;
        if (bitrate > 20000000)
            s.bitrateBonus = 10;
        if (bitrate > 50000000)
            s.bitrateBonus = 20;

        s.totalScore = s.gpuSupported + s.codecSupported + s.driverAvailable +
                       s.resolutionBonus + s.bitrateBonus + s.stabilityBonus + s.initPenalty;

        s.reasons << QString("Score %1: GPU=%2 Codec=%3 Driver=%4 Res=%5 Bitrate=%6 Stable=%7 Init=%8")
            .arg(s.totalScore)
            .arg(s.gpuSupported).arg(s.codecSupported).arg(s.driverAvailable)
            .arg(s.resolutionBonus).arg(s.bitrateBonus).arg(s.stabilityBonus).arg(s.initPenalty);
    }

    avcodec_free_context(&testCtx);
    av_buffer_unref(&hwCtx);

    return s;
}
