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

    m_renderer = RendererFactory::Create(RendererType::OpenGL).release();
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

    QMutexLocker lock(&m_mutex);
    QImage frame = m_frame;
    lock.unlock();

    if (frame.isNull())
        return;

    QRect outputRect = computeOutputRect(frame.size(), rect());
    if (outputRect.isEmpty())
        return;

    QPainter painter(this);
    m_renderer->paint(painter, outputRect);
}

QRect VideoWidget::computeOutputRect(const QSize &imgSize, const QRect &viewport) const
{
    QSize scaled = imgSize.scaled(viewport.size(), Qt::KeepAspectRatio);
    int x = (viewport.width() - scaled.width()) / 2;
    int y = (viewport.height() - scaled.height()) / 2;
    return QRect(x, y, scaled.width(), scaled.height());
}
