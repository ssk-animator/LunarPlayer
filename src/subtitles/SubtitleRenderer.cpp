#include "SubtitleRenderer.h"
#include "AssOverrideParser.h"
#include "AssAnimationEngine.h"
#include "AssClippingEngine.h"
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QTextLayout>
#include <QTextOption>

SubtitleRenderer::SubtitleRenderer()
{
    m_defaultStyle = SubtitleStyle();
}

SubtitleRenderer::~SubtitleRenderer() = default;

int SubtitleRenderer::scaledFontSize(int videoHeight, int baseSize)
{
    if (videoHeight <= 0) return baseSize;
    return qMax(12, (int)(baseSize * (videoHeight / 720.0)));
}

void SubtitleRenderer::setDefaultStyle(const SubtitleStyle &style)
{
    m_defaultStyle = style;
}

QVector<SubtitleSurface> SubtitleRenderer::render(const SubtitleFrame &frame,
                                                    int videoWidth, int videoHeight)
{
    switch (frame.type) {
    case SubtitleType::Bitmap:
        return renderBitmap(frame, videoWidth, videoHeight);
    case SubtitleType::Text:
        return renderText(frame, videoWidth, videoHeight);
    case SubtitleType::ASS:
        return renderASS(frame, videoWidth, videoHeight);
    default:
        return {};
    }
}

void SubtitleRenderer::render(QPainter &painter, const SubtitleFrame &frame,
                               int videoWidth, int videoHeight)
{
    QVector<SubtitleSurface> surfaces = render(frame, videoWidth, videoHeight);
    for (const SubtitleSurface &surface : surfaces) {
        if (surface.isNull()) continue;
        painter.drawImage(surface.posX(), surface.posY(), surface.toImage());
    }
}

QVector<SubtitleSurface> SubtitleRenderer::renderText(const SubtitleFrame &frame,
                                                       int videoWidth, int videoHeight)
{
    QVector<SubtitleSurface> result;
    const SubtitleStyle &s = frame.style;
    int fs = scaledFontSize(videoHeight, s.fontSize);

    QFont font(s.fontFamily, fs);
    font.setBold(s.bold);
    font.setItalic(s.italic);
    font.setUnderline(s.underline);
    font.setStrikeOut(s.strikethrough);

    QFontMetrics fm(font);
    int maxWidth = videoWidth - s.safeMarginX * 2;
    if (maxWidth <= 0) return result;

    QStringList words = frame.text.split(' ', Qt::SkipEmptyParts);
    QStringList lines;
    QString currentLine;
    for (const QString &word : words) {
        QString testLine = currentLine.isEmpty() ? word : currentLine + " " + word;
        if (fm.horizontalAdvance(testLine) > maxWidth && !currentLine.isEmpty()) {
            lines.append(currentLine);
            currentLine = word;
        } else {
            currentLine = testLine;
        }
    }
    if (!currentLine.isEmpty()) lines.append(currentLine);

    int lineHeight = fm.height() + 4;
    int textWidth = 0;
    for (const QString &line : lines)
        textWidth = qMax(textWidth, fm.horizontalAdvance(line));
    int totalHeight = (int)lines.size() * lineHeight;

    int padX = 12;
    int padY = 6;
    int imgW = textWidth + padX * 2;
    int imgH = totalHeight + padY * 2;
    QImage img(imgW, imgH, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing);

    int y = padY;
    for (const QString &line : lines) {
        int x = (imgW - fm.horizontalAdvance(line)) / 2;

        if (s.outlineWidth > 0) {
            p.setPen(s.outlineColor);
            p.setFont(font);
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    p.drawText(x + dx, y + dy, imgW, lineHeight,
                               Qt::AlignLeft | Qt::AlignTop, line);
                }
            }
        }

        QColor shadow = s.shadowColor;
        if (shadow.alpha() > 0 && s.shadowOffset > 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(shadow);
            p.drawText(x + s.shadowOffset, y + s.shadowOffset, imgW, lineHeight,
                       Qt::AlignLeft | Qt::AlignTop, line);
        }

        p.setPen(s.color);
        p.setFont(font);
        p.drawText(x, y, imgW, lineHeight, Qt::AlignLeft | Qt::AlignTop, line);
        y += lineHeight;
    }
    p.end();

    int screenX = (videoWidth - imgW) / 2;
    int screenY = videoHeight - imgH - frame.style.bottomMargin;
    screenX = qMax(frame.style.safeMarginX, screenX);
    screenY = qMax(frame.style.safeMarginY, screenY);

    result.append(SubtitleSurface(img, screenX, screenY));
    return result;
}

QVector<SubtitleSurface> SubtitleRenderer::renderBitmap(const SubtitleFrame &frame,
                                                         int videoWidth, int videoHeight)
{
    QVector<SubtitleSurface> result;
    Q_UNUSED(videoWidth);
    Q_UNUSED(videoHeight);

    if (frame.bitmap.isNull())
        return result;

    int x = frame.posX;
    int y = frame.posY;

    if (x == 0 && y == 0 && frame.displayWidth > 0) {
        x = (videoWidth - frame.bitmap.width()) / 2;
        y = videoHeight - frame.bitmap.height() - frame.style.bottomMargin;
    }

    result.append(SubtitleSurface(frame.bitmap, x, y));
    return result;
}

// ---- ASS Rendering Implementation ----

QVector<SubtitleSurface> SubtitleRenderer::renderASS(const SubtitleFrame &frame,
                                                      int videoWidth, int videoHeight)
{
    QVector<SubtitleSurface> result;

    SubtitleRenderContext ctx;
    ctx.videoSize = QSize(videoWidth, videoHeight);
    ctx.playRes = QSize(videoWidth, videoHeight); // default: video = ASS coords
    ctx.currentPTS = frame.renderPTS > 0
                     ? static_cast<double>(frame.renderPTS)
                     : frame.startSeconds * 1000.0;

    QVector<AssSegmentedLine> lines = AssOverrideParser::segment(frame.text, frame, ctx);
    if (lines.isEmpty()) return result;

    // Resolve animation for current PTS
    AssAnimationEngine::resolveLines(lines, ctx,
                                      frame.startSeconds, frame.endSeconds);

    // Measure total text block size
    double totalWidth = 0.0;
    double totalHeight = 0.0;
    double lineHeightEst = 0.0;

    double sxPlayRes = static_cast<double>(ctx.videoSize.width()) / ctx.playRes.width();
    double syPlayRes = static_cast<double>(ctx.videoSize.height()) / ctx.playRes.height();

    for (const auto &assLine : lines) {
        double lineW = 0.0;
        double lineH = 0.0;
        for (const auto &seg : assLine.segments) {
            if (seg.isDrawing) {
                QRectF bounds = seg.drawPath.boundingRect();
                double drawW = bounds.width() * sxPlayRes;
                double drawH = bounds.height() * syPlayRes;
                lineW += drawW;
                lineH = qMax(lineH, drawH);
            } else {
                QFontMetrics fm(seg.font);
                lineW += fm.horizontalAdvance(seg.text) * seg.scale.x();
                lineH = qMax(lineH, static_cast<double>(fm.height()) * seg.scale.y());
            }
        }
        totalWidth = qMax(totalWidth, lineW);
        totalHeight += lineH;
        lineHeightEst = qMax(lineHeightEst, lineH);
    }

    if (totalWidth <= 0.0 || totalHeight <= 0.0)
        return result;

    // Compute position from alignment or \pos
    PositionStyle posStyle;
    posStyle.alignment = frame.style.alignment;
    if (posStyle.alignment < 1 || posStyle.alignment > 9)
        posStyle.alignment = 2;
    posStyle.marginL = frame.style.safeMarginX;
    posStyle.marginR = frame.style.safeMarginY;
    posStyle.marginV = frame.style.bottomMargin;
    if (!lines.isEmpty() && !lines.first().segments.isEmpty()) {
        const auto &firstSeg = lines.first().segments.first();
        if (firstSeg.hasExplicitPos) {
            posStyle.hasPos = true;
            posStyle.posX = firstSeg.position.x();
            posStyle.posY = firstSeg.position.y();
        }
    }

    QPointF origin = computeAlignedPosition(
        QSize(static_cast<int>(totalWidth), static_cast<int>(totalHeight)),
        posStyle, ctx);

    // Render to canvas
    int canvasW = videoWidth;
    int canvasH = videoHeight;
    QImage canvas(canvasW, canvasH, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter canvasPainter(&canvas);
    canvasPainter.setRenderHint(QPainter::Antialiasing);
    canvasPainter.setRenderHint(QPainter::TextAntialiasing);

    renderSegments(canvasPainter, lines, ctx, origin);
    canvasPainter.end();

    result.append(SubtitleSurface(canvas, 0, 0));
    return result;
}

QPointF SubtitleRenderer::computeAlignedPosition(const QSize &textBlockSize,
                                                  const PositionStyle &pos,
                                                  const SubtitleRenderContext &ctx) const
{
    // \pos overrides alignment
    if (pos.hasPos) {
        double sx = static_cast<double>(ctx.videoSize.width()) / ctx.playRes.width();
        double sy = static_cast<double>(ctx.videoSize.height()) / ctx.playRes.height();
        return QPointF(pos.posX * sx, pos.posY * sy);
    }

    int align = qBound(1, pos.alignment, 9);
    int vw = ctx.videoSize.width();
    int vh = ctx.videoSize.height();
    int bw = textBlockSize.width();
    int bh = textBlockSize.height();

    double x = 0.0, y = 0.0;

    // Horizontal
    int hGroup = (align - 1) % 3; // 0=left, 1=center, 2=right
    switch (hGroup) {
    case 0: x = pos.marginL; break;
    case 1: x = (vw - bw) / 2.0; break;
    case 2: x = vw - bw - pos.marginR; break;
    }

    // Vertical
    int vGroup = (align - 1) / 3; // 0=bottom, 1=middle, 2=top
    switch (vGroup) {
    case 0: y = vh - bh - pos.marginV; break;
    case 1: y = (vh - bh) / 2.0; break;
    case 2: y = pos.marginV; break;
    }

    return QPointF(x, y);
}

void SubtitleRenderer::renderSegments(QPainter &painter,
                                       const QVector<AssSegmentedLine> &lines,
                                       const SubtitleRenderContext &ctx,
                                       const QPointF &origin)
{
    double lineY = origin.y();

    for (const auto &assLine : lines) {
        double lineX = origin.x();

        for (const auto &seg : assLine.segments) {
            if (seg.isDrawing) {
                // Drawing mode: render the QPainterPath directly
                painter.save();
                if (AssClippingEngine::hasActiveClip(seg)) {
                    QPainterPath clipPath = AssClippingEngine::buildClipPath(
                        seg, ctx.videoSize, ctx.playRes);
                    if (!clipPath.isEmpty())
                        painter.setClipPath(clipPath, Qt::ReplaceClip);
                }
                painter.translate(lineX, lineY);
                QPen drawPen(seg.primaryColor, 1.0);
                painter.setPen(drawPen);
                painter.setBrush(seg.primaryColor);
                painter.drawPath(seg.drawPath);
                painter.restore();
                continue;
            }

            if (seg.text.isEmpty()) continue;

            QFont renderFont = seg.font;
            if (renderFont.pointSizeF() <= 0.0)
                renderFont.setPointSizeF(12.0);

            // Save painter state for transform/opacity
            painter.save();

            // Apply clip before any translation (in global coordinates)
            if (AssClippingEngine::hasActiveClip(seg)) {
                QPainterPath clipPath = AssClippingEngine::buildClipPath(
                    seg, ctx.videoSize, ctx.playRes);
                if (!clipPath.isEmpty()) {
                    painter.setClipPath(clipPath, Qt::ReplaceClip);
                }
            }

            // Translate to text origin
            painter.translate(lineX, lineY);

            // Apply rotation transform (from \frx, \fry, \frz, or \t animation)
            if (!seg.transform.isIdentity())
                painter.setTransform(seg.transform, true);

            // Apply scale (from \fscx, \fscy, or \t animation)
            if (seg.scale.x() != 1.0 || seg.scale.y() != 1.0)
                painter.scale(seg.scale.x(), seg.scale.y());

            // Build or fetch text path from cache
            QPainterPath textPath;
            if (m_renderCache) {
                textPath = m_renderCache->getTextPath(renderFont, seg.text);
            }
            if (textPath.isEmpty()) {
                double fontSize = seg.font.pointSizeF();
                if (fontSize > 0.0) {
                    textPath.addText(0, 0, renderFont, seg.text);
                    if (m_renderCache)
                        m_renderCache->insertTextPath(renderFont, seg.text, textPath);
                }
            }
            if (textPath.isEmpty()) {
                painter.setFont(renderFont);
                painter.setPen(seg.primaryColor);
                painter.drawText(QPointF(0, 0), seg.text);
                painter.restore();
                QFontMetrics fm(renderFont);
                lineX += fm.horizontalAdvance(seg.text);
                continue;
            }

            // Opacity
            painter.setOpacity(seg.opacity);

            // Shadow (render at offset before outline/fill)
            if (seg.shadowColor.alpha() > 0) {
                QPainterPath shadowPath;
                QFont shadowFont = renderFont;
                shadowPath.addText(seg.shadowOffset.x(), seg.shadowOffset.y(),
                                   shadowFont, seg.text);
                painter.fillPath(shadowPath, seg.shadowColor);
            }

            // Outline (stroke the path)
            if (seg.outlineWidth > 0.0 && seg.outlineColor.alpha() > 0) {
                QPen outlinePen(seg.outlineColor, seg.outlineWidth * 2.0,
                                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                painter.strokePath(textPath, outlinePen);
            }

            // BorderStyle 3: opaque box background
            // (not yet implemented — will be added in optimization pass)

            // Primary fill (with optional karaoke two-color highlight)
            if (seg.hasKaraoke) {
                if (!seg.karaokeDrawnText.isEmpty()) {
                    QPainterPath drawnPath;
                    drawnPath.addText(0, 0, renderFont, seg.karaokeDrawnText);
                    painter.fillPath(drawnPath, seg.primaryColor);
                }
                if (!seg.karaokePendingText.isEmpty()) {
                    QFontMetrics km(renderFont);
                    double drawnW = km.horizontalAdvance(seg.karaokeDrawnText);
                    QPainterPath pendingPath;
                    pendingPath.addText(drawnW, 0, renderFont, seg.karaokePendingText);
                    painter.fillPath(pendingPath, seg.secondaryColor);
                }
            } else {
                painter.fillPath(textPath, seg.primaryColor);
            }

            painter.restore();
            painter.setOpacity(1.0);

            // Advance X for next segment (using original font metrics, not scaled)
            QFontMetrics fm(renderFont);
            lineX += fm.horizontalAdvance(seg.text) * seg.scale.x();
        }

        // Advance Y for next line
        QFontMetrics fm(lines.first().segments.isEmpty()
                        ? QFont() : lines.first().segments.first().font);
        lineY += fm.height();
    }
}
