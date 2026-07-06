#ifndef SUBTITLERENDERER_H
#define SUBTITLERENDERER_H

#include "SubtitleFrame.h"
#include "SubtitleSurface.h"
#include "AssTypes.h"
#include "AssRenderCache.h"
#include <QImage>
#include <QFont>
#include <QVector>
#include <QHash>

class SubtitleRenderer {
public:
    SubtitleRenderer();
    ~SubtitleRenderer();

    // Render one frame → one or more surfaces (single text line or composited bitmap)
    QVector<SubtitleSurface> render(const SubtitleFrame &frame,
                                     int videoWidth, int videoHeight);

    // Direct painter overlay (draws all surfaces at their positions)
    void render(QPainter &painter, const SubtitleFrame &frame,
                int videoWidth, int videoHeight);

    //     // Style management
    void setDefaultStyle(const SubtitleStyle &style);
    SubtitleStyle defaultStyle() const { return m_defaultStyle; }

    // Render cache (set by manager, owned elsewhere)
    void setRenderCache(AssRenderCache *cache) { m_renderCache = cache; }

    static int scaledFontSize(int videoHeight, int baseSize);

private:
    QVector<SubtitleSurface> renderText(const SubtitleFrame &frame,
                                         int videoWidth, int videoHeight);
    QVector<SubtitleSurface> renderBitmap(const SubtitleFrame &frame,
                                           int videoWidth, int videoHeight);
    QVector<SubtitleSurface> renderASS(const SubtitleFrame &frame,
                                        int videoWidth, int videoHeight);

    // ASS rendering helpers
    QPointF computeAlignedPosition(const QSize &textBlockSize,
                                    const PositionStyle &pos,
                                    const SubtitleRenderContext &ctx) const;

    void renderSegments(QPainter &painter,
                         const QVector<AssSegmentedLine> &lines,
                         const SubtitleRenderContext &ctx,
                         const QPointF &origin);

    SubtitleStyle m_defaultStyle;
    AssRenderCache *m_renderCache = nullptr;
};

#endif // SUBTITLERENDERER_H
