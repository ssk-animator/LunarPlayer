#include "AssAnimationEngine.h"
#include "KaraokeTimeline.h"
#include <QtMath>
#include <algorithm>

static void resolveLineKaraoke(AssSegmentedLine &line,
                                const SubtitleRenderContext &ctx,
                                double subStartSec, double subEndSec)
{
    if (line.segments.isEmpty()) return;

    // Collect karaoke segments in order
    struct KaraokeSegInfo { int segIndex; int duration; };
    QVector<KaraokeSegInfo> karaokeSegs;
    for (int i = 0; i < line.segments.size(); ++i) {
        if (line.segments[i].karaokeIndex >= 0 && line.segments[i].karaokeDuration > 0)
            karaokeSegs.append({i, line.segments[i].karaokeDuration});
    }
    if (karaokeSegs.isEmpty()) return;

    // Build karaoke syllables from segment durations
    QVector<KaraokeSyllable> syllables;
    for (const auto &ksi : karaokeSegs) {
        KaraokeSyllable syl;
        syl.duration = ksi.duration;
        syllables.append(syl);
    }

    double totalDurationSec = subEndSec - subStartSec;
    KaraokeTimeline timeline;
    timeline.build(syllables, subStartSec, totalDurationSec);
    if (timeline.isEmpty()) return;

    double currentSec = ctx.currentPTS / 1000.0;
    int activeSylIdx = timeline.activeSyllable(currentSec);
    double progress = timeline.syllableProgress(currentSec);
    const auto &timelineSyllables = timeline.syllables();

    for (int si = 0; si < karaokeSegs.size(); ++si) {
        int segIdx = karaokeSegs[si].segIndex;
        auto &seg = line.segments[segIdx];

        seg.hasKaraoke = true;

        if (activeSylIdx < 0) {
            if (currentSec < timelineSyllables.first().startSec) {
                seg.karaokeDrawnText.clear();
                seg.karaokePendingText = seg.text;
            } else {
                seg.karaokeDrawnText = seg.text;
                seg.karaokePendingText.clear();
            }
        } else if (si < activeSylIdx) {
            seg.karaokeDrawnText = seg.text;
            seg.karaokePendingText.clear();
        } else if (si == activeSylIdx) {
            int totalTextLen = seg.text.length();
            int splitPos = qBound(0, static_cast<int>(totalTextLen * progress), totalTextLen);
            seg.karaokeDrawnText = seg.text.left(splitPos);
            seg.karaokePendingText = seg.text.mid(splitPos);
        } else {
            seg.karaokeDrawnText.clear();
            seg.karaokePendingText = seg.text;
        }
    }
}

void AssAnimationEngine::resolveLines(QVector<AssSegmentedLine> &lines,
                                       const SubtitleRenderContext &ctx,
                                       double subStartSec, double subEndSec)
{
    for (auto &line : lines) {
        for (auto &seg : line.segments) {
            resolveSegment(seg, ctx, subStartSec, subEndSec);
        }
        resolveLineKaraoke(line, ctx, subStartSec, subEndSec);
    }
}

void AssAnimationEngine::resolveSegment(AssTextSegment &seg,
                                         const SubtitleRenderContext &ctx,
                                         double subStartSec, double subEndSec)
{
    // Resolve fade
    resolveFade(seg, ctx, subStartSec, subEndSec);

    // Resolve move
    if (seg.hasMove) {
        double progress = 0.0;
        if (seg.moveData.t2 > seg.moveData.t1) {
            double subPTS = ctx.currentPTS / 1000.0;
            double localTime = subPTS - subStartSec;
            double moveDuration = (seg.moveData.t2 - seg.moveData.t1) / 1000.0;
            if (moveDuration > 0.0) {
                progress = qBound(0.0,
                    (localTime * 1000.0 - seg.moveData.t1) / (seg.moveData.t2 - seg.moveData.t1),
                    1.0);
            }
        }
        resolveMove(seg, progress);
    }

    // Resolve transforms (\t())
    resolveTransforms(seg, ctx, subStartSec * 1000.0);

    // Apply rotation
    applyRotation(seg);
}

void AssAnimationEngine::resolveMove(AssTextSegment &seg, double progress)
{
    double x = seg.moveData.from.x() + (seg.moveData.to.x() - seg.moveData.from.x()) * progress;
    double y = seg.moveData.from.y() + (seg.moveData.to.y() - seg.moveData.from.y()) * progress;
    seg.position = QPointF(x, y);
    seg.hasExplicitPos = true;
}

void AssAnimationEngine::resolveFade(AssTextSegment &seg,
                                      const SubtitleRenderContext &ctx,
                                      double subStartSec, double subEndSec)
{
    double subStartMs = subStartSec * 1000.0;
    double subDurationMs = (subEndSec - subStartSec) * 1000.0;
    double currentMs = ctx.currentPTS;

    // Resolve based on fade type
    if (seg.fadeData.isFade) {
        // \fade(a1, a2, a3, t1, t2, t3) - alpha at three time points
        double t1 = seg.fadeData.t1 * 1000.0; // in ms relative to subtitle start
        double t2 = seg.fadeData.t2 * 1000.0;
        double t3 = seg.fadeData.t3 * 1000.0;
        double localMs = currentMs - subStartMs;

        int alpha = seg.fadeData.a2; // default: middle value
        if (localMs <= t1) {
            alpha = seg.fadeData.a1;
        } else if (localMs < t2 && t2 > t1) {
            double p = (localMs - t1) / (t2 - t1);
            alpha = static_cast<int>(seg.fadeData.a1 + (seg.fadeData.a2 - seg.fadeData.a1) * p);
        } else if (localMs >= t2 && localMs <= t3) {
            alpha = seg.fadeData.a2;
        } else if (localMs < t3 && t3 > t2) {
            double p = (localMs - t2) / (t3 - t2);
            alpha = static_cast<int>(seg.fadeData.a2 + (seg.fadeData.a3 - seg.fadeData.a2) * p);
        } else {
            alpha = seg.fadeData.a3;
        }
        // Convert ASS alpha (0=opaque, 255=transparent) to Qt alpha (255=opaque)
        seg.opacity = qBound(0.0, (255.0 - alpha) / 255.0, 1.0);
    } else if (seg.fadeData.fadeIn > 0.0 || seg.fadeData.fadeOut > 0.0) {
        // \fad(fadeInSec, fadeOutSec) - simple fade in/out
        double fadeInMs = seg.fadeData.fadeIn * 1000.0;
        double fadeOutMs = seg.fadeData.fadeOut * 1000.0;
        double localMs = currentMs - subStartMs;
        double endMs = subDurationMs;

        if (localMs < fadeInMs && fadeInMs > 0.0) {
            // Fading in
            seg.opacity = qBound(0.0, localMs / fadeInMs, 1.0);
        } else if (localMs > endMs - fadeOutMs && fadeOutMs > 0.0) {
            // Fading out
            double fadeLocal = localMs - (endMs - fadeOutMs);
            seg.opacity = qBound(0.0, 1.0 - fadeLocal / fadeOutMs, 1.0);
        } else {
            seg.opacity = 1.0;
        }
    }
}

void AssAnimationEngine::resolveTransforms(AssTextSegment &seg,
                                            const SubtitleRenderContext &ctx,
                                            double subStartMs)
{
    for (const auto &xf : seg.transforms) {
        double progress = computeProgress(xf.t1, xf.t2, xf.accel,
                                           ctx.currentPTS, subStartMs);
        if (progress <= 0.0) continue;

        for (const auto &target : xf.targets) {
            double targetVal = target.params.value(0, 0.0);
            // Use absolute interpolation: result = base + (target - base) * progress
            switch (target.type) {
            case AssTransform::Target::Frx:
                seg.rotationX = seg.rotationX + (targetVal - seg.rotationX) * progress;
                break;
            case AssTransform::Target::Fry:
                seg.rotationY = seg.rotationY + (targetVal - seg.rotationY) * progress;
                break;
            case AssTransform::Target::Frz:
                seg.rotationZ = seg.rotationZ + (targetVal - seg.rotationZ) * progress;
                break;
            case AssTransform::Target::Fscx:
                seg.scale.setX(seg.scale.x() + (targetVal / 100.0 - seg.scale.x()) * progress);
                break;
            case AssTransform::Target::Fscy:
                seg.scale.setY(seg.scale.y() + (targetVal / 100.0 - seg.scale.y()) * progress);
                break;
            case AssTransform::Target::Bord:
                seg.outlineWidth = seg.outlineWidth
                    + (targetVal - seg.outlineWidth) * progress;
                break;
            case AssTransform::Target::Shad: {
                double dx = target.params.value(0, 0.0);
                double dy = target.params.value(1, dx);
                double baseX = seg.shadowOffset.x();
                double baseY = seg.shadowOffset.y();
                seg.shadowOffset = QPointF(baseX + (dx - baseX) * progress,
                                            baseY + (dy - baseY) * progress);
                break;
            }
            case AssTransform::Target::Alpha: {
                int alphaTarget = static_cast<int>(targetVal);
                int base = seg.overrideAlpha >= 0 ? seg.overrideAlpha : 0;
                int resolved = base + static_cast<int>((alphaTarget - base) * progress);
                seg.opacity = qBound(0.0, (255.0 - resolved) / 255.0, 1.0);
                break;
            }
            case AssTransform::Target::Fs: {
                double currentSize = seg.font.pointSizeF();
                if (currentSize > 0.0) {
                    double newSize = currentSize + (targetVal - currentSize) * progress;
                    seg.font.setPointSizeF(qMax(1.0, newSize));
                }
                break;
            }
            case AssTransform::Target::Fsp:
                seg.tracking = seg.tracking
                    + (targetVal - seg.tracking) * progress;
                break;
            case AssTransform::Target::Fn: // font name - skip
            case AssTransform::Target::C1:
            case AssTransform::Target::C2:
            case AssTransform::Target::C3:
            case AssTransform::Target::C4:
            case AssTransform::Target::A1:
            case AssTransform::Target::A2:
            case AssTransform::Target::A3:
            case AssTransform::Target::A4:
            case AssTransform::Target::Clip:
            case AssTransform::Target::P:
                break;
            }
        }
    }
}

double AssAnimationEngine::computeProgress(double t1, double t2, double accel,
                                            double currentPTS, double subStartMs)
{
    double localMs = currentPTS - subStartMs;
    double duration = t2 - t1;
    if (duration <= 0.0) return 1.0;
    double raw = (localMs - t1) / duration;
    raw = qBound(0.0, raw, 1.0);
    if (accel > 0.0 && !qFuzzyCompare(accel, 1.0))
        raw = qPow(raw, 1.0 / accel);
    return raw;
}

void AssAnimationEngine::applyRotation(AssTextSegment &seg)
{
    if (qFuzzyIsNull(seg.rotationX) && qFuzzyIsNull(seg.rotationY)
        && qFuzzyIsNull(seg.rotationZ))
        return;

    QTransform xf;
    // Apply rotation around text center (approximate)
    if (!qFuzzyIsNull(seg.rotationZ))
        xf.rotate(seg.rotationZ);
    if (!qFuzzyIsNull(seg.rotationY))
        xf.rotate(seg.rotationY, Qt::YAxis);
    if (!qFuzzyIsNull(seg.rotationX))
        xf.rotate(seg.rotationX, Qt::XAxis);

    seg.transform = seg.transform * xf;
}
