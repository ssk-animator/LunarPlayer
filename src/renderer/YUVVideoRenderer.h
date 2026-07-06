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
    void paint(QPainter &painter, const RenderState &state) override;
    void resize(int w, int h) override;
    void cleanup() override;
    bool isValid() const override;

    bool supportsAVFrame() const override { return true; }
    void presentAVFrame(AVFrame *frame) override;
    FrameStats frameStats() const { return m_stats; }
    double lastUploadMs() const { return m_lastUploadMs; }

private:
    void initShaders();
    void initNV12Shader();
    void initP010Shader();
    void uploadYUV(const AVFrame *frame);
    void uploadYUV10bit(const AVFrame *frame);
    void uploadNV12(const AVFrame *frame);
    void uploadP010(const AVFrame *frame);
    bool is10bitFormat() const;

    AVPixelFormat m_pixFmt = AV_PIX_FMT_NONE;

    // YUV420P shader (3 planes)
    QOpenGLShaderProgram *m_program = nullptr;
    GLuint m_texY = 0, m_texU = 0, m_texV = 0;
    GLint m_uTexY = -1, m_uTexU = -1, m_uTexV = -1;
    GLint m_uScale = -1;

    // NV12 shader (2 planes: Y + interleaved UV)
    QOpenGLShaderProgram *m_nv12Program = nullptr;
    GLuint m_texUV = 0;
    GLint m_uTexY_nv12 = -1, m_uTexUV_nv12 = -1;

    // P010 shader (2 planes: Y 10-bit + interleaved UV 10-bit)
    QOpenGLShaderProgram *m_p010Program = nullptr;
    GLint m_uTexY_p010 = -1, m_uTexUV_p010 = -1;

    QOpenGLVertexArrayObject *m_vao = nullptr;
    GLuint m_vbo = 0;
    int m_texWidth = 0, m_texHeight = 0;
    int m_viewportW = 0, m_viewportH = 0;
    QMatrix4x4 m_projection;
    bool m_valid = false;

    FrameStats m_stats;
    double m_lastUploadMs = 0.0;
    QElapsedTimer m_frameTimer;
    QElapsedTimer m_fpsTimer;
    std::deque<double> m_frameTimes;
    int m_fpsFrameCount = 0;
};

#endif
