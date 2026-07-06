#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QImage>
#include <QVector>
#include <QMutex>
#include <QSettings>
#include "renderer/ColorManager.h"
#include "renderer/Renderer.h"

class Renderer;
struct AVFrame;
class CropAnalyzer;

class VideoWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    enum AspectMode { Auto, Original, Ratio16_9, Ratio21_9, Ratio4_3 };
    enum CropMode { None, Fill, Smart };

    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget() override;

    void setFrame(const QImage &frame);
    void setAVFrame(AVFrame *frame);
    void clearFrame();
    bool rendererSupportsAVFrame() const;
    Renderer* renderer() const { return m_renderer; }
    void setBottomPadding(int px) { m_bottomPadding = px; }
    void setShowPerformance(bool show) { m_showPerformance = show; update(); }
    void setPerformanceOverlay(const QString &text) { m_perfText = text; if (m_showPerformance) update(); }
    void setSubtitleImages(const QVector<QImage> &imgs) { m_subtitleImages = imgs; update(); }

    void setHDRMetadata(const HDRMetadata &metadata);

    // Zoom
    void setZoomFactor(double factor);
    double zoomFactor() const { return m_zoomFactor; }

    // Aspect Ratio
    void setAspectMode(AspectMode mode);
    AspectMode aspectMode() const { return m_aspectMode; }
    void setDAR(int darNum, int darDen);
    void clearDAR();

    // Crop
    void setCropMode(CropMode mode);
    CropMode cropMode() const { return m_cropMode; }

    // Source dimensions from media (used for aspect ratio / crop)
    void setSourceSize(int w, int h);

    // Persistence
    void saveState(QSettings &s) const;
    void restoreState(QSettings &s);

    // Invalidate smart crop cache (call on new file / seek / resolution change)
    void invalidateSmartCrop();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    QRectF computeDestination(const QSize &imgSize, const QRect &viewport) const;
    RenderState computeRenderState(const QSize &imgSize, const QRect &viewport);
    void detectSmartCrop(const QImage &frame);
    void detectSmartCropGL();

    QImage m_frame;
    QMutex m_mutex;
    Renderer *m_renderer = nullptr;
    int m_yuvWidth = 0, m_yuvHeight = 0;
    int m_bottomPadding = 0;
    bool m_showPerformance = false;
    QString m_perfText;
    QVector<QImage> m_subtitleImages;
    HDRMetadata m_hdrMetadata;

    // Zoom
    double m_zoomFactor = 1.0;

    // Aspect Ratio
    AspectMode m_aspectMode = Auto;
    int m_darNum = 0, m_darDen = 0;
    int m_sourceW = 0, m_sourceH = 0;

    // Crop
    CropMode m_cropMode = None;
    CropAnalyzer *m_cropAnalyzer = nullptr;
    QRectF m_smartCropSourceRect; // normalized source rect from smart crop (0..1)
    bool m_smartCropValid = false;
    bool m_smartCropDirty = true;
};

#endif
