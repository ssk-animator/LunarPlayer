#include "GLVideoRenderer.h"
#include <QOpenGLContext>
#include <QPainter>
#include <QDebug>

static const char *vertexBody = R"(
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
out vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

static const char *fragBody = R"(
in vec2 vTexCoord;
out vec4 fragColor;
uniform sampler2D uTexture;
void main() {
    fragColor = texture(uTexture, vTexCoord);
}
)";

GLVideoRenderer::~GLVideoRenderer()
{
    cleanup();
}

bool GLVideoRenderer::initialize()
{
    initializeOpenGLFunctions();

    auto *ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        qWarning("GLVideoRenderer: no current context");
        return false;
    }

    QByteArray verVert, verFrag;
    if (ctx->isOpenGLES()) {
        if (ctx->format().majorVersion() >= 3) {
            verVert = "#version 300 es\n";
            verFrag = "#version 300 es\nprecision mediump float;\n";
        } else {
            qWarning("GLVideoRenderer: OpenGL ES < 3.0 not supported");
            return false;
        }
    } else {
        int maj = ctx->format().majorVersion();
        int min = ctx->format().minorVersion();
        if (maj > 3 || (maj == 3 && min >= 3)) {
            verVert = "#version 330 core\n";
            verFrag = "#version 330 core\n";
        } else {
            qWarning("GLVideoRenderer: OpenGL < 3.3 not supported");
            return false;
        }
    }

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_program = new QOpenGLShaderProgram();
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, verVert + vertexBody)) {
        qWarning("GLVideoRenderer: vertex shader compilation failed");
        delete m_program;
        m_program = nullptr;
        return false;
    }
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, verFrag + fragBody)) {
        qWarning("GLVideoRenderer: fragment shader compilation failed");
        delete m_program;
        m_program = nullptr;
        return false;
    }
    if (!m_program->link()) {
        qWarning("GLVideoRenderer: shader program link failed");
        delete m_program;
        m_program = nullptr;
        return false;
    }
    m_texUniform = m_program->uniformLocation("uTexture");

    m_vao = new QOpenGLVertexArrayObject();
    if (!m_vao->create()) {
        qWarning("GLVideoRenderer: VAO creation failed");
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

    m_valid = true;
    return true;
}

void GLVideoRenderer::present(const QImage &frame)
{
    m_frame = frame;
}

void GLVideoRenderer::paint(QPainter &painter, const QRect &outputRect)
{
    if (!m_valid || m_frame.isNull())
        return;

    QImage src = m_frame.convertToFormat(QImage::Format_RGBA8888);
    if (src.isNull())
        return;

    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, src.width(), src.height(),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, src.constBits());

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
    m_program->setUniformValue(m_texUniform, 0);

    m_vao->bind();
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_vao->release();
    m_program->release();

    painter.endNativePainting();
}

void GLVideoRenderer::resize(int w, int h)
{
    m_viewportW = w;
    m_viewportH = h;
}

void GLVideoRenderer::cleanup()
{
    auto *ctx = QOpenGLContext::currentContext();

    if (ctx) {
        if (m_texture) {
            glDeleteTextures(1, &m_texture);
            m_texture = 0;
        }
        if (m_vbo) {
            glDeleteBuffers(1, &m_vbo);
            m_vbo = 0;
        }
        if (m_vao) {
            m_vao->destroy();
            delete m_vao;
            m_vao = nullptr;
        }
        if (m_program) {
            delete m_program;
            m_program = nullptr;
        }
    }

    m_valid = false;
    m_frame = QImage();
}

bool GLVideoRenderer::isValid() const
{
    return m_valid;
}
