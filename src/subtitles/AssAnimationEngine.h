#ifndef ASSANIMATIONENGINE_H
#define ASSANIMATIONENGINE_H

#include "AssTypes.h"
#include <QVector>

class AssAnimationEngine {
public:
    // Resolve all animation for a set of lines at a given PTS
    static void resolveLines(QVector<AssSegmentedLine> &lines,
                              const SubtitleRenderContext &ctx,
                              double subStartSec = 0.0,
                              double subEndSec = 0.0);

    // Resolve a single segment at current PTS
    static void resolveSegment(AssTextSegment &seg,
                                const SubtitleRenderContext &ctx,
                                double subStartSec, double subEndSec);

private:
    // Apply \move() interpolation
    static void resolveMove(AssTextSegment &seg, double progress);

    // Apply \fad()/\fade() opacity
    static void resolveFade(AssTextSegment &seg,
                             const SubtitleRenderContext &ctx,
                             double subStartSec, double subEndSec);

    // Apply \t() transforms at current PTS
    static void resolveTransforms(AssTextSegment &seg,
                                   const SubtitleRenderContext &ctx,
                                   double subStartSec);

    // Compute interpolation progress (0..1) with acceleration curve
    static double computeProgress(double t1, double t2, double accel,
                                   double currentPTS, double subStartMs);

    // Apply evaluated rotation to segment transform
    static void applyRotation(AssTextSegment &seg);
};

#endif // ASSANIMATIONENGINE_H
