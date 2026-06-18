#include "VideoWidget.h"
#include <QPainter>
#include <QOpenGLContext>

VideoWidget::VideoWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);
}

VideoWidget::~VideoWidget()
{
    makeCurrent();
    if (m_textureValid) {
        glDeleteTextures(1, &m_textureId);
    }
    doneCurrent();
}

void VideoWidget::setFrame(const QImage &frame)
{
    QMutexLocker lock(&m_mutex);
    m_frame = frame;
    update();
}

void VideoWidget::clearFrame()
{
    QMutexLocker lock(&m_mutex);
    m_frame = QImage();
    update();
}

void VideoWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.07f, 0.07f, 0.07f, 1.0f);
}

void VideoWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void VideoWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);

    QMutexLocker lock(&m_mutex);
    if (m_frame.isNull())
        return;
    QImage frame = m_frame;
    lock.unlock();

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QRect r = rect();
    QSize imgSize = frame.size();
    if (imgSize.isEmpty())
        return;

    QSize scaled = imgSize.scaled(r.size(), Qt::KeepAspectRatio);
    int x = (r.width() - scaled.width()) / 2;
    int y = (r.height() - scaled.height()) / 2;
    QRect drawRect(x, y, scaled.width(), scaled.height());

    painter.drawImage(drawRect, frame);
}
