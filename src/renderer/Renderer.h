#ifndef RENDERER_H
#define RENDERER_H

#include <QImage>
#include <QRect>
#include "ColorManager.h"

class QPainter;
struct AVFrame;

struct FrameStats {
    double fps = 0.0;
    double avgFrameMs = 0.0;
    double lastFrameMs = 0.0;
    int droppedFrames = 0;
};

struct RenderState {
    QRectF sourceRect;      // sub-region of the texture to sample (normalized 0..1, empty = full)
    QRectF destinationRect; // target rect in viewport coordinates
    float zoom = 1.0f;      // post-scale applied to destinationRect from center
};

class Renderer {
public:
    virtual ~Renderer() = default;

    virtual bool initialize() = 0;
    virtual void present(const QImage &frame) = 0;
    virtual void paint(QPainter &painter, const RenderState &state) = 0;
    virtual void resize(int w, int h) = 0;
    virtual void cleanup() = 0;
    virtual bool isValid() const = 0;

    virtual bool supportsAVFrame() const { return false; }
    virtual void presentAVFrame(AVFrame *frame) { Q_UNUSED(frame); }
    virtual FrameStats frameStats() const { return FrameStats{}; }
    virtual double lastUploadMs() const { return 0.0; }

    virtual void setHDRMetadata(const HDRMetadata &metadata) { m_hdrMetadata = metadata; }
    virtual HDRMetadata hdrMetadata() const { return m_hdrMetadata; }

protected:
    HDRMetadata m_hdrMetadata;
};

#endif
