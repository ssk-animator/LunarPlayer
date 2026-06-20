#ifndef YUVVIDEORENDERER_H
#define YUVVIDEORENDERER_H

#include "Renderer.h"
#include <QOpenGLFunctions>
#include <QOpenGLVertexArrayObject>
#include <QMatrix4x4>

extern "C" {
#include <libavutil/frame.h>
}

class QOpenGLShaderProgram;

class YUVVideoRenderer : public Renderer, protected QOpenGLFunctions {
public:
    YUVVideoRenderer();
    ~YUVVideoRenderer() override;

    bool initialize() override;
    void present(const QImage &frame) override;
    void paint(QPainter &painter, const QRect &outputRect) override;
    void resize(int w, int h) override;
    void cleanup() override;
    bool isValid() const override;

    bool supportsAVFrame() const override { return true; }
    void presentAVFrame(AVFrame *frame) override;

private:
    void initShader();
    void uploadYUV(const AVFrame *frame);

    QOpenGLShaderProgram *m_program = nullptr;
    QOpenGLVertexArrayObject *m_vao = nullptr;
    GLuint m_vbo = 0;
    GLuint m_texY = 0, m_texU = 0, m_texV = 0;
    int m_texWidth = 0, m_texHeight = 0;
    int m_viewportW = 0, m_viewportH = 0;
    QMatrix4x4 m_projection;
    bool m_valid = false;

    GLint m_uTexY = -1, m_uTexU = -1, m_uTexV = -1;
};

#endif
