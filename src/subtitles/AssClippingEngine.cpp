#include "AssClippingEngine.h"

QPainterPath AssClippingEngine::buildClipPath(const AssTextSegment &seg,
                                               const QSize &videoSize,
                                               const QSize &playRes)
{
    if (!seg.hasClip)
        return QPainterPath();

    double sx = static_cast<double>(videoSize.width()) / playRes.width();
    double sy = static_cast<double>(videoSize.height()) / playRes.height();

    QPainterPath clipPath;

    if (seg.clipRect.isValid()) {
        // Rectangular clip
        QRectF scaledRect(seg.clipRect.x() * sx,
                          seg.clipRect.y() * sy,
                          seg.clipRect.width() * sx,
                          seg.clipRect.height() * sy);
        clipPath.addRect(scaledRect);
    } else if (!seg.clipPath.isEmpty()) {
        // Vector clip — scale path coordinates
        QTransform scaleXf;
        scaleXf.scale(sx, sy);
        clipPath = scaleXf.map(seg.clipPath);
    } else {
        return QPainterPath();
    }

    if (seg.inverseClip) {
        // Inverse clip: full canvas minus clip region
        QPainterPath fullCanvas;
        fullCanvas.addRect(0, 0, videoSize.width(), videoSize.height());
        clipPath = fullCanvas.subtracted(clipPath);
    }

    return clipPath;
}

void AssClippingEngine::resolveClip(AssTextSegment &seg,
                                     const SubtitleRenderContext &ctx,
                                     double subStartMs)
{
    Q_UNUSED(ctx);
    Q_UNUSED(subStartMs);
    // Animated clip via \t() — will be implemented in future pass
    // For now, clip transforms in seg.transforms with Clip target are no-op
}

bool AssClippingEngine::hasActiveClip(const AssTextSegment &seg)
{
    return seg.hasClip;
}
