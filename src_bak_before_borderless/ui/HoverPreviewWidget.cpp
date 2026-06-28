#include "HoverPreviewWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QPainter>

HoverPreviewWidget::HoverPreviewWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFixedSize(m_thumbWidth + 4, m_thumbHeight + 22);
    hide();
}

void HoverPreviewWidget::showThumbnail(const QImage &thumb, const QString &timestamp)
{
    m_thumbnail = thumb;
    m_timestamp = timestamp;
    update();
    if (!isVisible())
        show();
}

void HoverPreviewWidget::hidePreview()
{
    hide();
}

void HoverPreviewWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Dark rounded background
    QPainterPath bg;
    bg.addRoundedRect(rect(), 6, 6);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(24, 24, 24, 230));
    p.drawPath(bg);

    // Thumbnail (inset 2px)
    if (!m_thumbnail.isNull()) {
        QRect thumbRect(2, 2, m_thumbWidth, m_thumbHeight);
        p.setClipRect(thumbRect);
        p.drawImage(thumbRect, m_thumbnail);
        p.setClipping(false);
    }

    // Thin border
    p.setPen(QPen(QColor(80, 80, 80, 160), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(0.5, 0.5, width() - 1, height() - 1), 6, 6);

    // Timestamp below
    QFont f = p.font();
    f.setPixelSize(11);
    f.setFamily("Segoe UI");
    p.setFont(f);
    p.setPen(QColor(220, 220, 220));
    QRect textRect(0, m_thumbHeight + 3, m_thumbWidth + 4, 17);
    p.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, m_timestamp);
}
