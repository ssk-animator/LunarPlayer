#ifndef ASSCLIPPINGENGINE_H
#define ASSCLIPPINGENGINE_H

#include "AssTypes.h"
#include <QPainterPath>

class AssClippingEngine {
public:
    // Build a QPainterPath from segment clip data, scaled to screen coordinates
    static QPainterPath buildClipPath(const AssTextSegment &seg,
                                      const QSize &videoSize,
                                      const QSize &playRes);

    // Resolve animated clip transforms (future: updates seg.clipRect/seg.clipPath)
    static void resolveClip(AssTextSegment &seg,
                            const SubtitleRenderContext &ctx,
                            double subStartMs);

    // Check if segment has active clip
    static bool hasActiveClip(const AssTextSegment &seg);
};

#endif // ASSCLIPPINGENGINE_H
