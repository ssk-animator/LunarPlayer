#ifndef CAPABILITYRENDERER_H
#define CAPABILITYRENDERER_H

#include "Renderer.h"
#include "FrameInfo.h"
#include "TextureUploader.h"
#include "ShaderManager.h"
#include <QOpenGLFunctions>
#include <QOpenGLVertexArrayObject>
#include <QMatrix4x4>
#include <QElapsedTimer>
#include <deque>

class QOpenGLShaderProgram;

class CapabilityRenderer : public Renderer, protected QOpenGLFunctions {
public:
    CapabilityRenderer();
    ~CapabilityRenderer() override;

    bool initialize() override;
    void present(const QImage &frame) override;
    void paint(QPainter &painter, const RenderState &state) override;
    void resize(int w, int h) override;
    void cleanup() override;
    bool isValid() const override;

    bool supportsAVFrame() const override { return true; }
    void presentAVFrame(AVFrame *frame) override;
    FrameStats frameStats() const { return m_stats; }

private:
    void uploadPlanarYUV(const AVFrame *frame, const FrameInfo &info);
    void uploadSemiPlanar(const AVFrame *frame, const FrameInfo &info);
    void uploadRGBFallback(const AVFrame *frame, const FrameInfo &info);
    ShaderType selectPlanarShader(int bitDepth) const;
    ShaderType selectSemiPlanarShader(int bitDepth) const;

    bool m_valid = false;
    FrameInfo m_currentInfo;

    TextureUploader *m_uploader = nullptr;
    ShaderManager *m_shaders = nullptr;

    TextureSlot m_slotY, m_slotU, m_slotV, m_slotUV;

    QOpenGLVertexArrayObject *m_vao = nullptr;
    GLuint m_vbo = 0;
    int m_viewportW = 0, m_viewportH = 0;

    FrameStats m_stats;
    QElapsedTimer m_frameTimer;
    QElapsedTimer m_fpsTimer;
    std::deque<double> m_frameTimes;
    int m_fpsFrameCount = 0;
};

#endif
