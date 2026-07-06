#include "FrameInfo.h"

extern "C" {
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
}

static int computeBitDepth(AVPixelFormat fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    if (!desc) return 8;
    return desc->comp[0].depth > 0 ? desc->comp[0].depth : 8;
}

static int computeChromaShift(AVPixelFormat fmt, int component)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    if (!desc) return 0;
    return desc->log2_chroma_w;
}

static MemoryLayout classifyLayout(AVPixelFormat fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    if (!desc) return MemoryLayout::Unknown;

    bool isPlanar = (desc->flags & AV_PIX_FMT_FLAG_PLANAR) != 0;
    bool isRGB = (desc->flags & AV_PIX_FMT_FLAG_RGB) != 0;
    int bits = desc->comp[0].depth;

    if (isRGB) {
        if (bits > 8) return MemoryLayout::FloatingPoint;
        return MemoryLayout::PackedRGB;
    }

    if (!isPlanar) {
        if (fmt == AV_PIX_FMT_NV12 || fmt == AV_PIX_FMT_NV21 ||
            fmt == AV_PIX_FMT_P010LE || fmt == AV_PIX_FMT_P010BE ||
            fmt == AV_PIX_FMT_P016LE || fmt == AV_PIX_FMT_P016BE)
            return MemoryLayout::SemiPlanar;
        return MemoryLayout::PackedYUV;
    }

    int hShift = desc->log2_chroma_w;
    int vShift = desc->log2_chroma_h;

    if (hShift == 1 && vShift == 1) {
        if (bits > 8) return MemoryLayout::HighBitDepth;
        return MemoryLayout::Planar420;
    }
    if (hShift == 1 && vShift == 0) {
        if (bits > 8) return MemoryLayout::HighBitDepth;
        return MemoryLayout::Planar422;
    }
    if (hShift == 0 && vShift == 0) {
        if (bits > 8) return MemoryLayout::HighBitDepth;
        return MemoryLayout::Planar444;
    }

    if (bits > 8) return MemoryLayout::HighBitDepth;
    return MemoryLayout::Unknown;
}

static HardwareBackend detectHardware(AVFrame *frame)
{
    if (!frame->hw_frames_ctx) return HardwareBackend::None;

    AVHWFramesContext *hwfc = reinterpret_cast<AVHWFramesContext *>(frame->hw_frames_ctx->data);
    if (!hwfc) return HardwareBackend::None;

    AVHWDeviceType type = hwfc->device_ctx->type;
    if (type == AV_HWDEVICE_TYPE_CUDA)    return HardwareBackend::CUDA;
    if (type == AV_HWDEVICE_TYPE_D3D11VA) return HardwareBackend::D3D11;
    if (type == AV_HWDEVICE_TYPE_DXVA2)   return HardwareBackend::DXVA2;
    if (type == AV_HWDEVICE_TYPE_VAAPI)   return HardwareBackend::VAAPI;
    if (type == AV_HWDEVICE_TYPE_QSV)     return HardwareBackend::QSV;
    if (type == AV_HWDEVICE_TYPE_AMF)     return HardwareBackend::AMF;
    if (type == AV_HWDEVICE_TYPE_VULKAN)  return HardwareBackend::Vulkan;
    return HardwareBackend::None;
}

FrameInfo FrameAnalyzer::analyze(const AVFrame *frame)
{
    FrameInfo info;

    info.pixelFormat = static_cast<AVPixelFormat>(frame->format);
    info.width = frame->width;
    info.height = frame->height;
    info.bitDepth = computeBitDepth(info.pixelFormat);
    info.chromaWidthShift = computeChromaShift(info.pixelFormat, 0);
    info.chromaHeightShift = computeChromaShift(info.pixelFormat, 1);
    info.layout = classifyLayout(info.pixelFormat);
    info.isHardwareFrame = (frame->hw_frames_ctx != nullptr);
    info.hardware = detectHardware(const_cast<AVFrame *>(frame));

    info.linesize[0] = frame->linesize[0];
    info.linesize[1] = frame->linesize[1];
    info.linesize[2] = frame->linesize[2];
    info.linesize[3] = frame->linesize[3];

    info.planeCount = av_pix_fmt_count_planes(info.pixelFormat);
    if (info.planeCount < 0) info.planeCount = 0;

    info.colorRange = frame->color_range;
    info.colorSpace = frame->colorspace;
    info.colorTransfer = frame->color_trc;
    info.colorPrimaries = frame->color_primaries;

    return info;
}
