#include "CPURenderer.h"
#include <QPainter>

CPURenderer::~CPURenderer() = default;

bool CPURenderer::initialize()
{
    m_colorManager.setDisplayMaxLuminance(300.0);
    return true;
}

void CPURenderer::present(const QImage &frame)
{
    m_frame = frame;
}

void CPURenderer::setHDRMetadata(const HDRMetadata &metadata)
{
    m_hdrMetadata = metadata;
    m_hdrActive = ColorManager::needsToneMapping(metadata);
}

void CPURenderer::paint(QPainter &painter, const RenderState &state)
{
    if (m_frame.isNull())
        return;

    QImage toDraw = m_frame;
    if (m_hdrActive) {
        toDraw = m_colorManager.applyToneMap(m_frame, m_hdrMetadata);
    }

    QRectF dest = state.destinationRect;
    // Apply zoom
    if (state.zoom != 1.0f) {
        float cx = dest.x() + dest.width() * 0.5f;
        float cy = dest.y() + dest.height() * 0.5f;
        float halfW = dest.width() * state.zoom * 0.5f;
        float halfH = dest.height() * state.zoom * 0.5f;
        dest = QRectF(cx - halfW, cy - halfH, halfW * 2.0f, halfH * 2.0f);
    }

    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (state.sourceRect.isValid()) {
        QRectF src(
            state.sourceRect.left() * toDraw.width(),
            state.sourceRect.top() * toDraw.height(),
            state.sourceRect.width() * toDraw.width(),
            state.sourceRect.height() * toDraw.height()
        );
        painter.drawImage(dest, toDraw, src);
    } else {
        painter.drawImage(dest, toDraw);
    }
}

void CPURenderer::resize(int, int)
{
}

void CPURenderer::cleanup()
{
    m_frame = QImage();
    m_hdrActive = false;
}

bool CPURenderer::isValid() const
{
    return true;
}
