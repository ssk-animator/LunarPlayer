#ifndef FRAMEINFO_H
#define FRAMEINFO_H

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

enum class MemoryLayout {
    Planar420,
    Planar422,
    Planar444,
    SemiPlanar,
    PackedRGB,
    PackedYUV,
    HighBitDepth,
    FloatingPoint,
    Unknown
};

enum class HardwareBackend {
    None,
    CUDA,
    D3D11,
    DXVA2,
    VAAPI,
    QSV,
    AMF,
    Vulkan
};

struct FrameInfo {
    AVPixelFormat pixelFormat = AV_PIX_FMT_NONE;
    int bitDepth = 0;
    int chromaWidthShift = 0;
    int chromaHeightShift = 0;
    MemoryLayout layout = MemoryLayout::Unknown;
    HardwareBackend hardware = HardwareBackend::None;
    bool isHardwareFrame = false;

    int colorRange = 0;
    int colorSpace = 0;
    int colorTransfer = 0;
    int colorPrimaries = 0;

    int width = 0;
    int height = 0;
    int linesize[4] = {};
    int planeCount = 0;

    bool is10bit() const { return bitDepth > 8; }
    bool isHighBitDepth() const { return bitDepth > 8; }
    bool hasHardwareFramesContext() const { return isHardwareFrame; }

    bool isPlanarYUV() const {
        return layout == MemoryLayout::Planar420 ||
               layout == MemoryLayout::Planar422 ||
               layout == MemoryLayout::Planar444;
    }

    bool isSemiPlanar() const { return layout == MemoryLayout::SemiPlanar; }
    bool isPackedRGB() const { return layout == MemoryLayout::PackedRGB; }
};

namespace FrameAnalyzer {
    FrameInfo analyze(const AVFrame *frame);
}

#endif
