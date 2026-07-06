#include "VideoWidget.h"
#include "renderer/Renderer.h"
#include "renderer/RendererFactory.h"
#include "renderer/CropAnalyzer.h"
#include <QPainter>
#include <QOpenGLContext>
#include <algorithm>

extern "C" {
#include <libavutil/frame.h>
}

VideoWidget::VideoWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);
    m_cropAnalyzer = new CropAnalyzer(this);
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

// ---- Zoom ----
void VideoWidget::setZoomFactor(double factor)
{
    m_zoomFactor = std::max(0.1, std::min(10.0, factor));
    update();
}

// ---- Aspect Ratio ----
void VideoWidget::setAspectMode(AspectMode mode)
{
    m_aspectMode = mode;
    update();
}

void VideoWidget::setDAR(int darNum, int darDen)
{
    m_darNum = darNum;
    m_darDen = darDen;
}

void VideoWidget::clearDAR()
{
    m_darNum = 0;
    m_darDen = 0;
}

// ---- Crop ----
void VideoWidget::setCropMode(CropMode mode)
{
    m_cropMode = mode;
    if (mode == Smart)
        m_smartCropDirty = true;
    update();
}

void VideoWidget::setSourceSize(int w, int h)
{
    m_sourceW = w;
    m_sourceH = h;
}

void VideoWidget::invalidateSmartCrop()
{
    m_smartCropValid = false;
    m_smartCropDirty = true;
    m_cropAnalyzer->invalidate();
}

// ---- Persistence ----
void VideoWidget::saveState(QSettings &s) const
{
    s.setValue("video/zoom", m_zoomFactor);
    s.setValue("video/aspect", static_cast<int>(m_aspectMode));
    s.setValue("video/crop", static_cast<int>(m_cropMode));
}

void VideoWidget::restoreState(QSettings &s)
{
    m_zoomFactor = s.value("video/zoom", 1.0).toDouble();
    m_aspectMode = static_cast<AspectMode>(s.value("video/aspect", static_cast<int>(Auto)).toInt());
    m_cropMode = static_cast<CropMode>(s.value("video/crop", static_cast<int>(None)).toInt());
    if (m_cropMode == Smart)
        m_smartCropDirty = true;
}

// ---- Frame ----
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
    // Ensure GL context is current — required for texture uploads outside paintGL()
    if (QOpenGLContext::currentContext() != context()) {
        makeCurrent();
    }
    m_renderer->presentAVFrame(frame);
    m_yuvWidth = frame->width;
    m_yuvHeight = frame->height;
    setSourceSize(frame->width, frame->height);
    update();
}

bool VideoWidget::rendererSupportsAVFrame() const
{
    return m_renderer && m_renderer->supportsAVFrame();
}

void VideoWidget::setHDRMetadata(const HDRMetadata &metadata)
{
    m_hdrMetadata = metadata;
    if (m_renderer)
        m_renderer->setHDRMetadata(metadata);
}

void VideoWidget::clearFrame()
{
    QMutexLocker lock(&m_mutex);
    m_frame = QImage();
    m_subtitleImages.clear();
    m_hdrMetadata = HDRMetadata();
    if (m_renderer) {
        m_renderer->setHDRMetadata(HDRMetadata());
        m_renderer->present(QImage());
    }
    update();
}

// ---- GL ----
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

    if (!m_renderer || !m_renderer->isValid()) {
        return;
    }

    QSize imgSize;

    {
        QMutexLocker lock(&m_mutex);
        if (!m_frame.isNull()) {
            imgSize = m_frame.size();
        } else if (m_renderer->supportsAVFrame() && m_yuvWidth > 0) {
            imgSize = QSize(m_yuvWidth, m_yuvHeight);
        }
    }

    if (imgSize.isEmpty()) {
        return;
    }

    QRect viewport = rect();
    if (m_bottomPadding > 0)
        viewport.adjust(0, 0, 0, -m_bottomPadding);

    RenderState state = computeRenderState(imgSize, viewport);
    if (state.destinationRect.isEmpty()) {
        return;
    }

    // Smart crop detection on the QImage path
    if (m_cropMode == Smart && m_smartCropDirty && !m_frame.isNull()) {
        detectSmartCrop(m_frame);
    }
    // For AVFrame path, smart crop runs once via setAVFrame dimensions
    if (m_cropMode == Smart && m_smartCropValid) {
        state.sourceRect = m_smartCropSourceRect;
    }

    QPainter painter(this);
    m_renderer->paint(painter, state);

    // Subtitle overlay
    for (const QImage &sub : m_subtitleImages) {
        if (!sub.isNull())
            painter.drawImage(0, 0, sub);
    }

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

// ---- Render State Computation ----
QRectF VideoWidget::computeDestination(const QSize &imgSize, const QRect &viewport) const
{
    QSize displaySize = imgSize;

    // Apply aspect ratio
    switch (m_aspectMode) {
    case Ratio16_9:
        displaySize = QSize(16, 9);
        break;
    case Ratio21_9:
        displaySize = QSize(21, 9);
        break;
    case Ratio4_3:
        displaySize = QSize(4, 3);
        break;
    case Original:
        break;
    case Auto:
    default:
        if (m_darNum > 0 && m_darDen > 0) {
            displaySize = QSize(m_darNum, m_darDen);
        }
        break;
    }

    Qt::AspectRatioMode arMode = Qt::KeepAspectRatio;
    if (m_cropMode == Fill)
        arMode = Qt::KeepAspectRatioByExpanding;

    QSize scaled = displaySize.scaled(viewport.size(), arMode);
    int x = (viewport.width() - scaled.width()) / 2;
    int y = (viewport.height() - scaled.height()) / 2;

    return QRectF(x, y, scaled.width(), scaled.height());
}

RenderState VideoWidget::computeRenderState(const QSize &imgSize, const QRect &viewport)
{
    RenderState state;
    state.zoom = static_cast<float>(m_zoomFactor);

    // Default source rect = full image in normalized UV coords (crop may override later)
    state.sourceRect = QRectF(0, 0, 1.0, 1.0);

    // Compute the destination rect (before zoom — zoom is applied by renderers)
    QRectF dst = computeDestination(imgSize, viewport);
    state.destinationRect = dst;

    return state;
}

// ---- Smart Crop ----
void VideoWidget::detectSmartCrop(const QImage &frame)
{
    if (!m_cropAnalyzer)
        return;

    QRectF crop = m_cropAnalyzer->detectBlackBars(frame);
    if (crop.isValid()) {
        m_smartCropSourceRect = crop;
        m_smartCropValid = true;
    } else {
        m_smartCropValid = false;
    }
    m_smartCropDirty = false;
}
