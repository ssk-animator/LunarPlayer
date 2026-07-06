#include "YUVVideoRenderer.h"
#include <QOpenGLShaderProgram>
#include <QOpenGLContext>
#include <QPainter>
#include <cmath>
#include <cstdio>
#include <vector>

extern "C" {
#include <libavutil/pixdesc.h>
}

static bool checkGL(const char *stage)
{
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "[RENDER] GL_ERROR in %s: 0x%04X\n", stage, err);
        return false;
    }
    return true;
}

YUVVideoRenderer::YUVVideoRenderer() = default;
YUVVideoRenderer::~YUVVideoRenderer() { cleanup(); }

bool YUVVideoRenderer::initialize()
{
    if (m_valid) return true;
    initializeOpenGLFunctions();

    initShaders();
    if (!m_program) return false;

    initNV12Shader();
    initP010Shader();

    m_vao = new QOpenGLVertexArrayObject();
    if (!m_vao->create()) {
        delete m_vao;
        m_vao = nullptr;
        delete m_program;
        if (m_nv12Program) { delete m_nv12Program; m_nv12Program = nullptr; }
        return false;
    }
    m_vao->bind();

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, 4 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    m_vao->release();

    glGenTextures(1, &m_texY);
    glGenTextures(1, &m_texU);
    glGenTextures(1, &m_texV);
    glGenTextures(1, &m_texUV);

    m_valid = true;
    return true;
}

void YUVVideoRenderer::initShaders()
{
    m_program = new QOpenGLShaderProgram();
    auto *ctx = QOpenGLContext::currentContext();

    QByteArray verVert, verFrag;
    if (ctx->isOpenGLES()) {
        verVert = "#version 300 es\n";
        verFrag = "#version 300 es\nprecision mediump float;\n";
    } else {
        verVert = "#version 330 core\n";
        verFrag = "#version 330 core\n";
    }

    const char *vsrc =
        "layout(location = 0) in vec2 aPos;\n"
        "layout(location = 1) in vec2 aTexCoord;\n"
        "out vec2 vTexCoord;\n"
        "void main() {\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "    vTexCoord = aTexCoord;\n"
        "}\n";

    const char *fsrc =
        "in vec2 vTexCoord;\n"
        "out vec4 fragColor;\n"
        "uniform sampler2D uTexY;\n"
        "uniform sampler2D uTexU;\n"
        "uniform sampler2D uTexV;\n"
        "uniform float uScale;\n"
        "void main() {\n"
        "    float y = texture(uTexY, vTexCoord).r * uScale;\n"
        "    float u = texture(uTexU, vTexCoord).r * uScale - 0.5;\n"
        "    float v = texture(uTexV, vTexCoord).r * uScale - 0.5;\n"
        "    float r = y + 1.402 * v;\n"
        "    float g = y - 0.344 * u - 0.714 * v;\n"
        "    float b = y + 1.772 * u;\n"
        "    fragColor = vec4(r, g, b, 1.0);\n"
        "}\n";

    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, verVert + vsrc) ||
        !m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, verFrag + fsrc) ||
        !m_program->link()) {
        delete m_program;
        m_program = nullptr;
        return;
    }

    m_uTexY = m_program->uniformLocation("uTexY");
    m_uTexU = m_program->uniformLocation("uTexU");
    m_uTexV = m_program->uniformLocation("uTexV");
    m_uScale = m_program->uniformLocation("uScale");
}

void YUVVideoRenderer::initNV12Shader()
{
    m_nv12Program = new QOpenGLShaderProgram();
    auto *ctx = QOpenGLContext::currentContext();

    QByteArray verVert, verFrag;
    if (ctx->isOpenGLES()) {
        verVert = "#version 300 es\n";
        verFrag = "#version 300 es\nprecision mediump float;\n";
    } else {
        verVert = "#version 330 core\n";
        verFrag = "#version 330 core\n";
    }

    const char *vsrc =
        "layout(location = 0) in vec2 aPos;\n"
        "layout(location = 1) in vec2 aTexCoord;\n"
        "out vec2 vTexCoord;\n"
        "void main() {\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "    vTexCoord = aTexCoord;\n"
        "}\n";

    const char *fsrc =
        "in vec2 vTexCoord;\n"
        "out vec4 fragColor;\n"
        "uniform sampler2D uTexY;\n"
        "uniform sampler2D uTexUV;\n"
        "void main() {\n"
        "    float y = texture(uTexY, vTexCoord).r;\n"
        "    vec2 uv = texture(uTexUV, vTexCoord).rg;\n"
        "    float u = uv.r - 0.5;\n"
        "    float v = uv.g - 0.5;\n"
        "    float r = y + 1.402 * v;\n"
        "    float g = y - 0.344 * u - 0.714 * v;\n"
        "    float b = y + 1.772 * u;\n"
        "    fragColor = vec4(r, g, b, 1.0);\n"
        "}\n";

    if (!m_nv12Program->addShaderFromSourceCode(QOpenGLShader::Vertex, verVert + vsrc) ||
        !m_nv12Program->addShaderFromSourceCode(QOpenGLShader::Fragment, verFrag + fsrc) ||
        !m_nv12Program->link()) {
        delete m_nv12Program;
        m_nv12Program = nullptr;
        return;
    }

    m_uTexY_nv12 = m_nv12Program->uniformLocation("uTexY");
    m_uTexUV_nv12 = m_nv12Program->uniformLocation("uTexUV");
}

bool YUVVideoRenderer::is10bitFormat() const
{
    return m_pixFmt == AV_PIX_FMT_YUV420P10LE ||
           m_pixFmt == AV_PIX_FMT_P010LE;
}

void YUVVideoRenderer::initP010Shader()
{
    m_p010Program = new QOpenGLShaderProgram();
    auto *ctx = QOpenGLContext::currentContext();

    QByteArray verVert, verFrag;
    if (ctx->isOpenGLES()) {
        verVert = "#version 300 es\n";
        verFrag = "#version 300 es\nprecision highp float;\n";
    } else {
        verVert = "#version 330 core\n";
        verFrag = "#version 330 core\n";
    }

    const char *vsrc =
        "layout(location = 0) in vec2 aPos;\n"
        "layout(location = 1) in vec2 aTexCoord;\n"
        "out vec2 vTexCoord;\n"
        "void main() {\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "    vTexCoord = aTexCoord;\n"
        "}\n";

    const char *fsrc =
        "in vec2 vTexCoord;\n"
        "out vec4 fragColor;\n"
        "uniform sampler2D uTexY;\n"
        "uniform sampler2D uTexUV;\n"
        "void main() {\n"
        "    float y = texture(uTexY, vTexCoord).r;\n"
        "    vec2 uv = texture(uTexUV, vTexCoord).rg;\n"
        "    float u = uv.r - 0.5;\n"
        "    float v = uv.g - 0.5;\n"
        "    float r = y + 1.402 * v;\n"
        "    float g = y - 0.344 * u - 0.714 * v;\n"
        "    float b = y + 1.772 * u;\n"
        "    fragColor = vec4(r, g, b, 1.0);\n"
        "}\n";

    if (!m_p010Program->addShaderFromSourceCode(QOpenGLShader::Vertex, verVert + vsrc) ||
        !m_p010Program->addShaderFromSourceCode(QOpenGLShader::Fragment, verFrag + fsrc) ||
        !m_p010Program->link()) {
        delete m_p010Program;
        m_p010Program = nullptr;
        return;
    }

    m_uTexY_p010 = m_p010Program->uniformLocation("uTexY");
    m_uTexUV_p010 = m_p010Program->uniformLocation("uTexUV");
}

void YUVVideoRenderer::present(const QImage &frame)
{
    Q_UNUSED(frame);
}

void YUVVideoRenderer::presentAVFrame(AVFrame *frame)
{
    if (!frame || !frame->data[0] || !m_valid)
        return;

    double elapsed = m_frameTimer.isValid() ? m_frameTimer.nsecsElapsed() / 1000000.0 : 0.0;
    m_frameTimer.start();

    m_pixFmt = static_cast<AVPixelFormat>(frame->format);

    QElapsedTimer uploadTimer; uploadTimer.start();

    switch (m_pixFmt) {
    case AV_PIX_FMT_NV12:
        uploadNV12(frame);
        break;
    case AV_PIX_FMT_YUV420P10LE:
        uploadYUV10bit(frame);
        break;
    case AV_PIX_FMT_P010LE:
        uploadP010(frame);
        break;
    default:
        uploadYUV(frame);
        break;
    }

    double uploadMs = uploadTimer.nsecsElapsed() / 1000000.0;
    m_stats.lastFrameMs = elapsed;
    m_lastUploadMs = uploadMs;
    m_frameTimes.push_back(elapsed);
    while (m_frameTimes.size() > 120)
        m_frameTimes.pop_front();

    double sum = 0.0;
    for (double t : m_frameTimes) sum += t;
    m_stats.avgFrameMs = m_frameTimes.empty() ? 0.0 : sum / m_frameTimes.size();

    m_fpsFrameCount++;
    if (!m_fpsTimer.isValid()) {
        m_fpsTimer.start();
    } else {
        double sec = m_fpsTimer.nsecsElapsed() / 1000000000.0;
        if (sec >= 1.0) {
            m_stats.fps = m_fpsFrameCount / sec;
            m_fpsFrameCount = 0;
            m_fpsTimer.restart();
        }
    }
}

void YUVVideoRenderer::uploadYUV(const AVFrame *frame)
{
    int w = frame->width;
    int h = frame->height;
    int uw = (w + 1) / 2;
    int uh = (h + 1) / 2;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texY);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, frame->data[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_texU);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, uw, uh, 0, GL_RED, GL_UNSIGNED_BYTE, frame->data[1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_texV);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[2]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, uw, uh, 0, GL_RED, GL_UNSIGNED_BYTE, frame->data[2]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    m_texWidth = w;
    m_texHeight = h;
}

void YUVVideoRenderer::uploadYUV10bit(const AVFrame *frame)
{
    if (!isValid() || !frame) return;

    int w = frame->width;
    int h = frame->height;
    int uw = (w + 1) / 2;
    int uh = (h + 1) / 2;

    bool needsRecreate = (m_texWidth != w || m_texHeight != h);
    if (needsRecreate) {
        if (m_texY) { glDeleteTextures(1, &m_texY); m_texY = 0; }
        if (m_texU) { glDeleteTextures(1, &m_texU); m_texU = 0; }
        if (m_texV) { glDeleteTextures(1, &m_texV); m_texV = 0; }

        glGenTextures(1, &m_texY);
        glGenTextures(1, &m_texU);
        glGenTextures(1, &m_texV);

        m_texWidth = w;
        m_texHeight = h;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texY);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[0] / 2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16, w, h, 0, GL_RED, GL_UNSIGNED_SHORT, frame->data[0]);
    if (needsRecreate) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_texU);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[1] / 2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16, uw, uh, 0, GL_RED, GL_UNSIGNED_SHORT, frame->data[1]);
    if (needsRecreate) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_texV);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[2] / 2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16, uw, uh, 0, GL_RED, GL_UNSIGNED_SHORT, frame->data[2]);
    if (needsRecreate) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void YUVVideoRenderer::uploadP010(const AVFrame *frame)
{
    int w = frame->width;
    int h = frame->height;
    int uw = (w + 1) / 2;
    int uh = (h + 1) / 2;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texY);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[0] / 2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16, w, h, 0, GL_RED, GL_UNSIGNED_SHORT, frame->data[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_texUV);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[1] / 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16, uw, uh, 0, GL_RG, GL_UNSIGNED_SHORT, frame->data[1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    m_texWidth = w;
    m_texHeight = h;
}

void YUVVideoRenderer::uploadNV12(const AVFrame *frame)
{
    int w = frame->width;
    int h = frame->height;
    int uw = (w + 1) / 2;
    int uh = (h + 1) / 2;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texY);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, frame->data[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_texUV);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, uw, uh, 0, GL_RG, GL_UNSIGNED_BYTE, frame->data[1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    m_texWidth = w;
    m_texHeight = h;
}

void YUVVideoRenderer::paint(QPainter &painter, const RenderState &state)
{
    if (!m_valid || !m_texWidth || !m_texHeight)
        return;

    const QRectF &dst = state.destinationRect;
    float vw = m_viewportW > 0 ? 2.0f / m_viewportW : 2.0f;
    float vh = m_viewportH > 0 ? 2.0f / m_viewportH : 2.0f;

    float cx = dst.x() + dst.width() * 0.5f;
    float cy = dst.y() + dst.height() * 0.5f;
    float halfW = dst.width() * state.zoom * 0.5f;
    float halfH = dst.height() * state.zoom * 0.5f;
    float left = (cx - halfW) * vw - 1.0f;
    float right = (cx + halfW) * vw - 1.0f;
    float top = -((cy - halfH) * vh - 1.0f);
    float bottom = -((cy + halfH) * vh - 1.0f);

    // Compute texture coordinates from sourceRect (or full texture if empty)
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    if (state.sourceRect.isValid()) {
        u0 = state.sourceRect.left();
        v0 = state.sourceRect.top();
        u1 = state.sourceRect.right();
        v1 = state.sourceRect.bottom();
    }

    float vertices[] = {
        left,  bottom, u0, v1,
        right, bottom, u1, v1,
        left,  top,    u0, v0,
        right, top,    u1, v0
    };

    painter.beginNativePainting();

    if (m_pixFmt == AV_PIX_FMT_NV12 && m_nv12Program) {
        m_nv12Program->bind();
        m_nv12Program->setUniformValue(m_uTexY_nv12, 0);
        m_nv12Program->setUniformValue(m_uTexUV_nv12, 1);

        m_vao->bind();
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_texY);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_texUV);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        m_vao->release();
        m_nv12Program->release();
    } else if (m_pixFmt == AV_PIX_FMT_P010LE && m_p010Program) {
        m_p010Program->bind();
        m_p010Program->setUniformValue(m_uTexY_p010, 0);
        m_p010Program->setUniformValue(m_uTexUV_p010, 1);

        m_vao->bind();
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_texY);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_texUV);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        m_vao->release();
        m_p010Program->release();
    } else if (m_program) {
        m_program->bind();
        m_program->setUniformValue(m_uTexY, 0);
        m_program->setUniformValue(m_uTexU, 1);
        m_program->setUniformValue(m_uTexV, 2);
        float scale = is10bitFormat() ? (65535.0f / 1023.0f) : 1.0f;
        m_program->setUniformValue(m_uScale, scale);

        m_vao->bind();
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_texY);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_texU);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_texV);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        m_vao->release();
        m_program->release();
    }

    painter.endNativePainting();
}

void YUVVideoRenderer::resize(int w, int h)
{
    m_viewportW = w;
    m_viewportH = h;
}

void YUVVideoRenderer::cleanup()
{
    auto *ctx = QOpenGLContext::currentContext();
    if (ctx) {
        if (m_texY) { glDeleteTextures(1, &m_texY); m_texY = 0; }
        if (m_texU) { glDeleteTextures(1, &m_texU); m_texU = 0; }
        if (m_texV) { glDeleteTextures(1, &m_texV); m_texV = 0; }
        if (m_texUV) { glDeleteTextures(1, &m_texUV); m_texUV = 0; }
        if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
        if (m_vao) { m_vao->destroy(); delete m_vao; m_vao = nullptr; }
        if (m_program) { delete m_program; m_program = nullptr; }
        if (m_nv12Program) { delete m_nv12Program; m_nv12Program = nullptr; }
        if (m_p010Program) { delete m_p010Program; m_p010Program = nullptr; }
    }
    m_valid = false;
    m_texWidth = m_texHeight = 0;
    m_pixFmt = AV_PIX_FMT_NONE;
}

bool YUVVideoRenderer::isValid() const
{
    return m_valid;
}
