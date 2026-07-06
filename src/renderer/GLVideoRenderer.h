#ifndef GLVIDEORENDERER_H
#define GLVIDEORENDERER_H

#include "Renderer.h"
#include "GPUTextureCache.h"
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>

class GLVideoRenderer : public Renderer, protected QOpenGLExtraFunctions {
public:
    GLVideoRenderer() = default;
    ~GLVideoRenderer() override;

    bool initialize() override;
    void present(const QImage &frame) override;
    void paint(QPainter &painter, const RenderState &state) override;
    void resize(int w, int h) override;
    void cleanup() override;
    bool isValid() const override;

    void setHDRMetadata(const HDRMetadata &metadata) override;

    GPUTextureCache* textureCache() { return &m_textureCache; }

private:
    bool initShaders();
    QOpenGLShaderProgram* selectProgram();
    void uploadToTexture(const QImage &src);

    bool m_valid = false;
    QImage m_frame;
    GLuint m_texture = 0;
    GPUTextureCache m_textureCache;

    QOpenGLShaderProgram *m_sdrProgram = nullptr;
    QOpenGLShaderProgram *m_hdrProgram = nullptr;
    QOpenGLShaderProgram *m_activeProgram = nullptr;

    QOpenGLVertexArrayObject *m_vao = nullptr;
    GLuint m_vbo = 0;
    int m_texUniform = -1;
    int m_hdrMaxLumUniform = -1;
    int m_hdrExposureUniform = -1;
    int m_hdrModeUniform = -1;
    int m_hdrDisplayMaxUniform = -1;
    int m_viewportW = 1;
    int m_viewportH = 1;
    int m_toneMapMode = 0; // 0=off, 1=Reinhard, 2=Hable
    double m_currentMaxLuminance = 1000.0;
    double m_currentExposure = 0.0;
    double m_displayMaxLuminance = 300.0;
};

#endif
