#ifndef ASSTYPES_H
#define ASSTYPES_H

#include <QColor>
#include <QFont>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QTransform>
#include <QVector>

// ---- ASS metadata extracted from [Script Info] ----
struct AssScriptInfo {
    QSize playRes{384, 288};
    QString title;
    QString scriptType = "v4.00+";
    QString wrapStyle = "0";
    QString collisions = "Normal";
};

// ---- Font-related style (maps to \fn, \fs, \b, \i, \u, \s, \fscx, \fscy, \fsp) ----
struct FontStyle {
    QString family = "Arial";
    int size = 28;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    double scaleX = 100.0;
    double scaleY = 100.0;
    double spacing = 0.0;
    double angle = 0.0;
    int encoding = 1;

    bool operator==(const FontStyle &o) const {
        return family == o.family && size == o.size && bold == o.bold
            && italic == o.italic && underline == o.underline
            && strikethrough == o.strikethrough && scaleX == o.scaleX
            && scaleY == o.scaleY && spacing == o.spacing && angle == o.angle
            && encoding == o.encoding;
    }
    bool operator!=(const FontStyle &o) const { return !(*this == o); }
};

// ---- Color and alpha (maps to \1c-\4c, \1a-\4a, \alpha) ----
struct ColorStyle {
    QColor primary{Qt::white};
    QColor secondary{Qt::black};
    QColor outline{Qt::black};
    QColor shadow{QColor(0, 0, 0, 0)};

    bool operator==(const ColorStyle &o) const {
        return primary == o.primary && secondary == o.secondary
            && outline == o.outline && shadow == o.shadow;
    }
    bool operator!=(const ColorStyle &o) const { return !(*this == o); }
};

// ---- Outline, shadow, blur (maps to \bord, \shad, \be, \blur, BorderStyle) ----
struct OutlineShadowStyle {
    int borderStyle = 1;
    double outlineWidth = 2.0;
    double shadowX = 0.0;
    double shadowY = 2.0;
    double blur = 0.0;

    bool operator==(const OutlineShadowStyle &o) const {
        return borderStyle == o.borderStyle && outlineWidth == o.outlineWidth
            && shadowX == o.shadowX && shadowY == o.shadowY && blur == o.blur;
    }
    bool operator!=(const OutlineShadowStyle &o) const { return !(*this == o); }
};

// ---- Position and alignment (maps to \pos, \move, alignment, margins) ----
struct PositionStyle {
    int alignment = 2;
    bool hasPos = false;
    double posX = 0.0;
    double posY = 0.0;
    int marginL = 20;
    int marginR = 20;
    int marginV = 20;

    bool operator==(const PositionStyle &o) const {
        return alignment == o.alignment && hasPos == o.hasPos
            && posX == o.posX && posY == o.posY
            && marginL == o.marginL && marginR == o.marginR && marginV == o.marginV;
    }
    bool operator!=(const PositionStyle &o) const { return !(*this == o); }
};

// ---- Movement data parsed from \move() ----
struct AssMoveData {
    QPointF from;
    QPointF to;
    double t1 = 0.0;
    double t2 = 0.0;
};

// ---- Fade data parsed from \fad() and \fade() ----
struct AssFadeData {
    double fadeIn = 0.0;   // seconds
    double fadeOut = 0.0;  // seconds
    // \fade params: a1, a2, a3, t1, t2, t3
    int a1 = 0, a2 = 255, a3 = 0;
    double t1 = 0.0, t2 = 0.0, t3 = 0.0;
    bool isFade = false;   // true for \fade, false for \fad
};

// ---- Karaoke timing (parsed from \k, \kf, \ko tags) ----
struct KaraokeSyllable {
    int duration = 0;
    QString text;
};

// ---- Drawing mode (parsed from \p) ----
struct DrawingCommand {
    enum Type { MoveTo, LineTo, Bezier, SmoothBezier, ClosePath, CubicBezier };
    Type type;
    QVector<QPointF> points;
};

// ---- One \t() transform definition ----
struct AssTransform {
    double accel = 1.0;
    double t1 = 0.0;
    double t2 = -1.0;
    struct Target {
        enum Type { Fscx, Fscy, Frx, Fry, Frz, Bord, Shad, Alpha, Clip, P,
                    C1, C2, C3, C4, A1, A2, A3, A4, Fs, Fsp, Fn };
        Type type;
        QVector<double> params;
    };
    QVector<Target> targets;
};

// ---- Clip data from \clip and \iclip ----
struct AssClipData {
    QRectF rect;
    QPainterPath path;
    bool isRect = true; // true = rectangular, false = vector
    bool hasClip = false;
    bool inverse = false;
};

// ---- Fully resolved render instruction consumed by SubtitleRenderer ----
struct SubtitleRenderItem {
    QString text;
    QPainterPath path;
    bool isDrawing = false;

    QFont font;
    QColor primaryColor;
    QColor outlineColor;
    QColor shadowColor;

    double outlineWidth = 0.0;
    QPointF shadowOffset;
    double opacity = 1.0;
    QTransform transform;
    QRectF clipRect;
    bool hasClip = false;
    bool inverseClip = false;
    QPointF position;
    int layer = 0;
};

// ---- Context passed to renderer for every frame ----
struct SubtitleRenderContext {
    QSize videoSize;
    QSize playRes{384, 288};
    double currentPTS = 0.0;
    double playbackSpeed = 1.0;
    bool compareMode = false;
};

// ---- Named style from [V4+ Styles] section ----
struct AssStyleDefinition {
    QString name = "Default";
    FontStyle font;
    ColorStyle colors;
    OutlineShadowStyle outlineShadow;
    PositionStyle position;
    QString fontName;
};

// ---- One renderable text segment after override resolution ----
struct AssTextSegment {
    QString text;
    QFont font;
    QColor primaryColor;
    QColor outlineColor;
    QColor shadowColor;
    QColor secondaryColor;
    double outlineWidth = 0.0;
    QPointF shadowOffset;
    double opacity = 1.0;
    QTransform transform;
    QPointF position;
    bool hasExplicitPos = false;
    QRectF clipRect;
    QPainterPath clipPath;
    bool hasClip = false;
    bool inverseClip = false;
    QVector<AssTransform> clipTransforms;
    int karaokeIndex = -1;
    int karaokeDuration = 0;
    QPainterPath drawPath;
    bool isDrawing = false;
    double blur = 0.0;

    // Animation data (populated by parser, resolved by AssAnimationEngine)
    AssMoveData moveData;
    bool hasMove = false;
    AssFadeData fadeData;
    QVector<AssTransform> transforms;
    double rotationX = 0.0;
    double rotationY = 0.0;
    double rotationZ = 0.0;
    int overrideAlpha = -1; // -1 = not overridden, 0-255
    QPointF scale{1.0, 1.0}; // \fscx, \fscy as multipliers
    double tracking = 0.0;    // \fsp

    // Karaoke resolved data (populated by AssAnimationEngine)
    QString karaokeDrawnText;
    QString karaokePendingText;
    bool hasKaraoke = false;
};

// ---- Collection of segments for one subtitle line of text ----
struct AssSegmentedLine {
    QVector<AssTextSegment> segments;
    double totalWidth = 0.0;
    double height = 0.0;
    QVector<KaraokeSyllable> karaokeSyllables;
};

#endif // ASSTYPES_H
