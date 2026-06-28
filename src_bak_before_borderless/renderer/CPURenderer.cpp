#include "CPURenderer.h"
#include <QPainter>

CPURenderer::~CPURenderer() = default;

bool CPURenderer::initialize()
{
    return true;
}

void CPURenderer::present(const QImage &frame)
{
    m_frame = frame;
}

void CPURenderer::paint(QPainter &painter, const QRect &outputRect)
{
    if (m_frame.isNull())
        return;
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(outputRect, m_frame);
}

void CPURenderer::resize(int, int)
{
}

void CPURenderer::cleanup()
{
    m_frame = QImage();
}

bool CPURenderer::isValid() const
{
    return true;
}
