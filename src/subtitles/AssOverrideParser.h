#ifndef ASSOVERRIDEPARSER_H
#define ASSOVERRIDEPARSER_H

#include "AssTypes.h"
#include "SubtitleFrame.h"
#include <QString>
#include <QVector>
#include <QHash>

class AssOverrideParser {
public:
    struct OverrideState {
        FontStyle font;
        ColorStyle colors;
        OutlineShadowStyle outlineShadow;
        PositionStyle position;
        bool drawingMode = false;
        int drawScale = 0;
        QPainterPath drawPath;
        QVector<KaraokeSyllable> karaoke;
        int karaokeTotal = 0;
        QVector<AssTransform> transforms;
        double fadeIn = 0.0;
        double fadeOut = 0.0;
        double rotationX = 0.0;
        double rotationY = 0.0;
        double rotationZ = 0.0;
        int overrideAlpha = -1;
        AssFadeData fadeData;
        AssMoveData moveData;
        bool hasMove = false;
        double scaleX = 100.0;
        double scaleY = 100.0;
        double tracking = 0.0;
        AssClipData clip;
    };

    // Split raw ASS text into a list of segments, applying all overrides
    // styleMap is optional; if empty, frame.style fields are used as base
    static QVector<AssSegmentedLine> segment(
        const QString &assText,
        const SubtitleFrame &frame,
        const SubtitleRenderContext &ctx,
        const QHash<QString, AssStyleDefinition> &styleMap = QHash<QString, AssStyleDefinition>());

    // Parse a single {...} override block into state changes
    static OverrideState parseOverrideBlock(const QString &block,
                                             const OverrideState &current,
                                             const FontStyle &baseFont,
                                             const QSize &playRes);

    // Parse drawing commands (m n l b s c p) from text into a QPainterPath
    static QPainterPath parseDrawingCommands(const QString &text, double scale = 1.0);

private:
    static void applyTag(const QString &tag, OverrideState &state,
                          const FontStyle &baseFont, const QSize &playRes);
    static QVector<KaraokeSyllable> parseKaraoke(const QString &text,
                                                    const OverrideState &state);
};

#endif // ASSOVERRIDEPARSER_H
