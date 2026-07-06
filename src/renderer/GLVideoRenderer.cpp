#include "GLVideoRenderer.h"
#include <QOpenGLContext>
#include <QPainter>
#include <cmath>

static const char *vertexBody = R"(
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
out vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

static const char *fragSDR = R"(
in vec2 vTexCoord;
out vec4 fragColor;
uniform sampler2D uTexture;
void main() {
    fragColor = texture(uTexture, vTexCoord);
}
)";

static const char *fragHDR = R"(
in vec2 vTexCoord;
out vec4 fragColor;
uniform sampler2D uTexture;
uniform float uMaxLuminance;
uniform float uExposure;
uniform int uToneMapMode;
uniform float uDisplayMaxLuminance;

vec3 reinhardToneMap(vec3 color, float exposure) {
    color = color * exposure;
    return color / (color + vec3(1.0));
}

vec3 hableToneMap(vec3 color) {
    color = color * 1.2;
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    return ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
}

vec3 acesToneMap(vec3 color) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

void main() {
    vec4 color = texture(uTexture, vTexCoord);
    vec3 rgb = color.rgb;

    float exposure = pow(2.0, uExposure);

    if (uToneMapMode == 1) {
        rgb = reinhardToneMap(rgb, exposure);
    } else if (uToneMapMode == 2) {
        rgb = hableToneMap(rgb);
    } else if (uToneMapMode == 3) {
        rgb = acesToneMap(rgb);
    }

    fragColor = vec4(clamp(rgb, 0.0, 1.0), color.a);
}
)";

GLVideoRenderer::~GLVideoRenderer()
{
    cleanup();
}

bool GLVideoRenderer::initShaders()
{
    auto *ctx = QOpenGLContext::currentContext();
    if (!ctx) return false;

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

    // SDR program (passthrough)
    m_sdrProgram = new QOpenGLShaderProgram();
    if (!m_sdrProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, verVert + vertexBody)) {
        qWarning("GLVideoRenderer: SDR vertex shader compilation failed");
        return false;
    }
    if (!m_sdrProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, verFrag + fragSDR)) {
        qWarning("GLVideoRenderer: SDR fragment shader compilation failed");
        return false;
    }
    if (!m_sdrProgram->link()) {
        qWarning("GLVideoRenderer: SDR shader program link failed");
        return false;
    }

    // HDR program (tone mapping)
    m_hdrProgram = new QOpenGLShaderProgram();
    if (!m_hdrProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, verVert + vertexBody)) {
        qWarning("GLVideoRenderer: HDR vertex shader compilation failed");
        return false;
    }
    if (!m_hdrProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, verFrag + fragHDR)) {
        qWarning("GLVideoRenderer: HDR fragment shader compilation failed");
        return false;
    }
    if (!m_hdrProgram->link()) {
        qWarning("GLVideoRenderer: HDR shader program link failed");
        return false;
    }

    m_texUniform = m_sdrProgram->uniformLocation("uTexture");
    m_hdrMaxLumUniform = m_hdrProgram->uniformLocation("uMaxLuminance");
    m_hdrExposureUniform = m_hdrProgram->uniformLocation("uExposure");
    m_hdrModeUniform = m_hdrProgram->uniformLocation("uToneMapMode");
    m_hdrDisplayMaxUniform = m_hdrProgram->uniformLocation("uDisplayMaxLuminance");

    return true;
}

void GLVideoRenderer::setHDRMetadata(const HDRMetadata &metadata)
{
    m_hdrMetadata = metadata;
    bool needsTM = (metadata.transfer == TransferCharacteristics::PQ ||
                    metadata.transfer == TransferCharacteristics::HLG);

    if (m_hdrMetadata.format == HDRFormat::None || !needsTM) {
        m_toneMapMode = 0;
    } else {
        if (metadata.transfer == TransferCharacteristics::PQ) {
            m_toneMapMode = 1; // Reinhard for PQ
        } else if (metadata.transfer == TransferCharacteristics::HLG) {
            m_toneMapMode = 2; // Hable for HLG
        } else {
            m_toneMapMode = 1;
        }

        m_currentMaxLuminance = metadata.maxLuminance;
        m_currentExposure = 0.0;
    }
}

QOpenGLShaderProgram* GLVideoRenderer::selectProgram()
{
    return (m_toneMapMode > 0) ? m_hdrProgram : m_sdrProgram;
}

bool GLVideoRenderer::initialize()
{
    initializeOpenGLFunctions();

    auto *ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        qWarning("GLVideoRenderer: no current context");
        return false;
    }

    if (!m_textureCache.initialize()) {
        qWarning("GLVideoRenderer: texture cache initialization failed");
        return false;
    }

    if (!initShaders()) {
        qWarning("GLVideoRenderer: shader initialization failed");
        return false;
    }

    m_activeProgram = m_sdrProgram;

    m_vao = new QOpenGLVertexArrayObject();
    if (!m_vao->create()) {
        qWarning("GLVideoRenderer: VAO creation failed");
        delete m_vao;
        m_vao = nullptr;
        delete m_sdrProgram;
        m_sdrProgram = nullptr;
        delete m_hdrProgram;
        m_hdrProgram = nullptr;
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

void GLVideoRenderer::uploadToTexture(const QImage &src)
{
    // Upload the frame texture directly (video frames change every frame,
    // so caching is not beneficial here — GPUTextureCache is for subtitle reuse)
    if (m_texture == 0) {
        glGenTextures(1, &m_texture);
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, src.width(), src.height(),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, src.constBits());
}

void GLVideoRenderer::paint(QPainter &painter, const RenderState &state)
{
    if (!m_valid || m_frame.isNull())
        return;

    QImage src = m_frame.convertToFormat(QImage::Format_RGBA8888);
    if (src.isNull())
        return;

    uploadToTexture(src);
    if (m_texture == 0)
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

    m_activeProgram = selectProgram();

    painter.beginNativePainting();

    m_activeProgram->bind();
    m_activeProgram->setUniformValue(m_texUniform, 0);

    if (m_activeProgram == m_hdrProgram) {
        m_activeProgram->setUniformValue(m_hdrMaxLumUniform, static_cast<float>(m_currentMaxLuminance));
        m_activeProgram->setUniformValue(m_hdrExposureUniform, static_cast<float>(m_currentExposure));
        m_activeProgram->setUniformValue(m_hdrModeUniform, m_toneMapMode);
        m_activeProgram->setUniformValue(m_hdrDisplayMaxUniform, static_cast<float>(m_displayMaxLuminance));
    }

    m_vao->bind();
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_vao->release();
    m_activeProgram->release();

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
        m_textureCache.clear();
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
        if (m_sdrProgram) {
            delete m_sdrProgram;
            m_sdrProgram = nullptr;
        }
        if (m_hdrProgram) {
            delete m_hdrProgram;
            m_hdrProgram = nullptr;
        }
    }

    m_valid = false;
    m_frame = QImage();
    m_activeProgram = nullptr;
}

bool GLVideoRenderer::isValid() const
{
    return m_valid;
}
