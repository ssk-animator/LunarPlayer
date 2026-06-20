#ifndef CPURENDERER_H
#define CPURENDERER_H

#include "Renderer.h"

class CPURenderer : public Renderer {
public:
    CPURenderer() = default;
    ~CPURenderer() override;

    bool initialize() override;
    void present(const QImage &frame) override;
    void paint(QPainter &painter, const QRect &outputRect) override;
    void resize(int w, int h) override;
    void cleanup() override;
    bool isValid() const override;

private:
    QImage m_frame;
};

#endif
