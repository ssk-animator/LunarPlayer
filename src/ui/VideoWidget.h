#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QImage>
#include <QMutex>

class Renderer;
struct AVFrame;

class VideoWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget() override;

    void setFrame(const QImage &frame);
    void setAVFrame(AVFrame *frame);
    void clearFrame();
    bool rendererSupportsAVFrame() const;

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    QRect computeOutputRect(const QSize &imgSize, const QRect &viewport) const;

    QImage m_frame;
    QMutex m_mutex;
    Renderer *m_renderer = nullptr;
};

#endif
