#include "VideoWidget.h"
#include "renderer/Renderer.h"
#include "renderer/RendererFactory.h"
#include <QPainter>
#include <QOpenGLContext>

extern "C" {
#include <libavutil/frame.h>
}

VideoWidget::VideoWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);
}

VideoWidget::~VideoWidget()
{
    makeCurrent();
    if (m_renderer) {
        m_renderer->cleanup();
        delete m_renderer;
        m_renderer = nullptr;
    }
    doneCurrent();
}

void VideoWidget::setFrame(const QImage &frame)
{
    QMutexLocker lock(&m_mutex);
    m_frame = frame;
    if (m_renderer)
        m_renderer->present(frame);
    update();
}

void VideoWidget::setAVFrame(AVFrame *frame)
{
    if (!m_renderer || !m_renderer->supportsAVFrame() || !frame)
        return;
    m_renderer->presentAVFrame(frame);
    m_yuvWidth = frame->width;
    m_yuvHeight = frame->height;
    update();
}

bool VideoWidget::rendererSupportsAVFrame() const
{
    return m_renderer && m_renderer->supportsAVFrame();
}

void VideoWidget::clearFrame()
{
    QMutexLocker lock(&m_mutex);
    m_frame = QImage();
    if (m_renderer)
        m_renderer->present(QImage());
    update();
}

void VideoWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.07f, 0.07f, 0.07f, 1.0f);

    m_renderer = RendererFactory::Create(RendererType::OpenGLYUV).release();
    m_renderer->initialize();
}

void VideoWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
    if (m_renderer)
        m_renderer->resize(w, h);
}

void VideoWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);

    if (!m_renderer || !m_renderer->isValid())
        return;

    QSize imgSize;

    QMutexLocker lock(&m_mutex);
    if (!m_frame.isNull()) {
        imgSize = m_frame.size();
    } else if (m_renderer->supportsAVFrame() && m_yuvWidth > 0) {
        imgSize = QSize(m_yuvWidth, m_yuvHeight);
    }
    lock.unlock();

    if (imgSize.isEmpty())
        return;

    QRect viewport = rect();
    if (m_bottomPadding > 0)
        viewport.adjust(0, 0, 0, -m_bottomPadding);

    QRect outputRect = computeOutputRect(imgSize, viewport);
    if (outputRect.isEmpty())
        return;

    QPainter painter(this);
    m_renderer->paint(painter, outputRect);

    if (m_showPerformance && !m_perfText.isEmpty()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 160));
        QFont font("Consolas", 11);
        painter.setFont(font);

        QFontMetrics fm(font);
        QStringList lines = m_perfText.split('\n');
        int lineH = fm.height() + 2;
        int totalH = lines.size() * lineH + 8;
        int totalW = 0;
        for (const QString &l : lines)
            totalW = qMax(totalW, fm.horizontalAdvance(l) + 12);

        QRect bgRect(8, 8, totalW, totalH);
        painter.drawRoundedRect(bgRect, 4, 4);

        painter.setPen(QColor(0, 255, 100));
        int y = 8 + fm.ascent() + 2;
        for (const QString &l : lines) {
            painter.drawText(14, y, l);
            y += lineH;
        }
    }
}

QRect VideoWidget::computeOutputRect(const QSize &imgSize, const QRect &viewport) const
{
    QSize scaled = imgSize.scaled(viewport.size(), Qt::KeepAspectRatio);
    int x = (viewport.width() - scaled.width()) / 2;
    int y = (viewport.height() - scaled.height()) / 2;
    return QRect(x, y, scaled.width(), scaled.height());
}
