#ifndef SUBTITLEFRAME_H
#define SUBTITLEFRAME_H

#include <QImage>
#include <QString>
#include <QColor>
#include <QRect>
#include <cstdint>

enum class SubtitleType {
    Text,     // SRT, VTT, plain text
    ASS,      // ASS/SSA with inline markup
    Bitmap,   // PGS, DVD, DVB
    Future    // placeholder for new codec types
};

struct SubtitleStyle {
    QString fontFamily = "Arial";
    int fontSize = 28;
    QColor color = Qt::white;
    QColor outlineColor = Qt::black;
    int outlineWidth = 2;
    QColor shadowColor = QColor(0, 0, 0, 180);
    int shadowOffset = 2;
    int bottomMargin = 40;
    int safeMarginX = 20;
    int safeMarginY = 20;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    QColor backgroundColor;
    int alignment = 2; // ASS: 1-9 (2=bottom-center)
};

struct SubtitleFrame {
    int64_t pts = 0;
    int64_t duration = 0;
    int64_t endPts() const { return pts + duration; }

    SubtitleType type = SubtitleType::Text;
    QString text;
    QImage bitmap;
    SubtitleStyle style;

    double startSeconds = 0.0;
    double endSeconds = 0.0;

    // Positioning for bitmap subtitles
    int posX = 0;
    int posY = 0;
    int displayWidth = 0;
    int displayHeight = 0;
    bool forced = false;

    // Current render time in ms (set by SubtitleManager before rendering)
    int64_t renderPTS = 0;

    // Layer ordering (ASS)
    int layer = 0;

    // ASS named style
    QString styleName;

    // Error recovery (non-fatal issues during parsing)
    QString errorMsg;

    // Stable cache identity
    int trackIndex = -1;
    int subtitleIndex = -1;

    bool isActive(int64_t currentPts) const {
        return currentPts >= pts && currentPts < pts + duration;
    }
    bool isActiveSec(double currentSec) const {
        return currentSec >= startSeconds && currentSec < endSeconds;
    }

    // Stable cache key: (trackIndex << 48) | (subtitleIndex << 32) | pts
    uint64_t cacheKey() const {
        return (static_cast<uint64_t>(qMax(trackIndex, 0)) << 48)
             | (static_cast<uint64_t>(qMax(subtitleIndex, 0)) << 32)
             | static_cast<uint64_t>(pts);
    }
};

#endif // SUBTITLEFRAME_H
