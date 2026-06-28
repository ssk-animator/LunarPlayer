#ifndef YUVVIDEORENDERER_H
#define YUVVIDEORENDERER_H

#include "Renderer.h"
#include <QOpenGLFunctions>
#include <QOpenGLVertexArrayObject>
#include <QMatrix4x4>
#include <QElapsedTimer>
#include <deque>

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
    FrameStats frameStats() const { return m_stats; }

private:
    void initShaders();
    void initNV12Shader();
    void uploadYUV(const AVFrame *frame);
    void uploadNV12(const AVFrame *frame);

    AVPixelFormat m_pixFmt = AV_PIX_FMT_NONE;

    // YUV420P shader (3 planes)
    QOpenGLShaderProgram *m_program = nullptr;
    GLuint m_texY = 0, m_texU = 0, m_texV = 0;
    GLint m_uTexY = -1, m_uTexU = -1, m_uTexV = -1;

    // NV12 shader (2 planes: Y + interleaved UV)
    QOpenGLShaderProgram *m_nv12Program = nullptr;
    GLuint m_texUV = 0;
    GLint m_uTexY_nv12 = -1, m_uTexUV_nv12 = -1;

    QOpenGLVertexArrayObject *m_vao = nullptr;
    GLuint m_vbo = 0;
    int m_texWidth = 0, m_texHeight = 0;
    int m_viewportW = 0, m_viewportH = 0;
    QMatrix4x4 m_projection;
    bool m_valid = false;

    FrameStats m_stats;
    QElapsedTimer m_frameTimer;
    QElapsedTimer m_fpsTimer;
    std::deque<double> m_frameTimes;
    int m_fpsFrameCount = 0;
};

#endif
