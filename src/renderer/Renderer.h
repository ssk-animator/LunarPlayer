#ifndef RENDERER_H
#define RENDERER_H

#include <QImage>
#include <QRect>

class QPainter;
struct AVFrame;

class Renderer {
public:
    virtual ~Renderer() = default;

    virtual bool initialize() = 0;
    virtual void present(const QImage &frame) = 0;
    virtual void paint(QPainter &painter, const QRect &outputRect) = 0;
    virtual void resize(int w, int h) = 0;
    virtual void cleanup() = 0;
    virtual bool isValid() const = 0;

    virtual bool supportsAVFrame() const { return false; }
    virtual void presentAVFrame(AVFrame *frame) { Q_UNUSED(frame); }
};

#endif
