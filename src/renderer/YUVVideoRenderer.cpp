#include "YUVVideoRenderer.h"
#include <QOpenGLShaderProgram>
#include <QOpenGLContext>
#include <QPainter>
#include <cmath>

YUVVideoRenderer::YUVVideoRenderer() = default;
YUVVideoRenderer::~YUVVideoRenderer() { cleanup(); }

bool YUVVideoRenderer::initialize()
{
    if (m_valid) return true;
    initializeOpenGLFunctions();

    initShader();
    if (!m_program) return false;

    m_vao = new QOpenGLVertexArrayObject();
    if (!m_vao->create()) {
        delete m_vao;
        m_vao = nullptr;
        delete m_program;
        m_program = nullptr;
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

    m_valid = true;
    return true;
}

void YUVVideoRenderer::initShader()
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
        "void main() {\n"
        "    float y = texture(uTexY, vTexCoord).r;\n"
        "    float u = texture(uTexU, vTexCoord).r - 0.5;\n"
        "    float v = texture(uTexV, vTexCoord).r - 0.5;\n"
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
}

void YUVVideoRenderer::present(const QImage &frame)
{
    Q_UNUSED(frame);
}

void YUVVideoRenderer::presentAVFrame(AVFrame *frame)
{
    if (!frame || !frame->data[0] || !m_valid)
        return;
    uploadYUV(frame);
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

void YUVVideoRenderer::paint(QPainter &painter, const QRect &outputRect)
{
    if (!m_valid || !m_texWidth || !m_texHeight)
        return;

    glBindTexture(GL_TEXTURE_2D, m_texY);

    float w = m_viewportW > 0 ? 2.0f / m_viewportW : 2.0f;
    float h = m_viewportH > 0 ? 2.0f / m_viewportH : 2.0f;
    float left = outputRect.x() * w - 1.0f;
    float right = (outputRect.x() + outputRect.width()) * w - 1.0f;
    float top = -(outputRect.y() * h - 1.0f);
    float bottom = -((outputRect.y() + outputRect.height()) * h - 1.0f);

    float vertices[] = {
        left,  bottom, 0.0f, 1.0f,
        right, bottom, 1.0f, 1.0f,
        left,  top,    0.0f, 0.0f,
        right, top,    1.0f, 0.0f
    };

    painter.beginNativePainting();

    m_program->bind();
    m_program->setUniformValue(m_uTexY, 0);
    m_program->setUniformValue(m_uTexU, 1);
    m_program->setUniformValue(m_uTexV, 2);

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
        if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
        if (m_vao) { m_vao->destroy(); delete m_vao; m_vao = nullptr; }
        if (m_program) { delete m_program; m_program = nullptr; }
    }
    m_valid = false;
    m_texWidth = m_texHeight = 0;
}

bool YUVVideoRenderer::isValid() const
{
    return m_valid;
}
