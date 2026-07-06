#ifndef CPURENDERER_H
#define CPURENDERER_H

#include "Renderer.h"
#include "ColorManager.h"

class CPURenderer : public Renderer {
public:
    CPURenderer() = default;
    ~CPURenderer() override;

    bool initialize() override;
    void present(const QImage &frame) override;
    void paint(QPainter &painter, const RenderState &state) override;
    void resize(int w, int h) override;
    void cleanup() override;
    bool isValid() const override;

    void setHDRMetadata(const HDRMetadata &metadata) override;
    using Renderer::setHDRMetadata;

private:
    QImage m_frame;
    ColorManager m_colorManager;
    bool m_hdrActive = false;
};

#endif
