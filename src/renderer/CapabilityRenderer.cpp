#include "CapabilityRenderer.h"
#include <QOpenGLShaderProgram>
#include <QOpenGLContext>
#include <QPainter>
#include <cmath>

CapabilityRenderer::CapabilityRenderer() = default;
CapabilityRenderer::~CapabilityRenderer() { cleanup(); }

bool CapabilityRenderer::initialize()
{
    if (m_valid) return true;
    initializeOpenGLFunctions();

    m_shaders = new ShaderManager();
    m_shaders->initialize(this);

    m_uploader = new TextureUploader(this);

    m_vao = new QOpenGLVertexArrayObject();
    if (!m_vao->create()) {
        delete m_vao;
        m_vao = nullptr;
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

    glGenTextures(1, &m_slotY.texId);
    glGenTextures(1, &m_slotU.texId);
    glGenTextures(1, &m_slotV.texId);
    glGenTextures(1, &m_slotUV.texId);

    m_valid = true;
    return true;
}

void CapabilityRenderer::present(const QImage &frame)
{
    Q_UNUSED(frame);
}

void CapabilityRenderer::presentAVFrame(AVFrame *frame)
{
    if (!frame || !frame->data[0] || !m_valid)
        return;

    double elapsed = m_frameTimer.isValid() ? m_frameTimer.nsecsElapsed() / 1000000.0 : 0.0;
    m_frameTimer.start();

    m_currentInfo = FrameAnalyzer::analyze(frame);

    if (m_currentInfo.layout == MemoryLayout::PackedRGB) {
        uploadRGBFallback(frame, m_currentInfo);
    } else if (m_currentInfo.isSemiPlanar()) {
        uploadSemiPlanar(frame, m_currentInfo);
    } else if (m_currentInfo.isPlanarYUV()) {
        uploadPlanarYUV(frame, m_currentInfo);
    } else {
        uploadPlanarYUV(frame, m_currentInfo);
    }

    m_stats.lastFrameMs = elapsed;
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

ShaderType CapabilityRenderer::selectPlanarShader(int bitDepth) const
{
    if (bitDepth <= 8) return ShaderType::PlanarYUV8;
    if (bitDepth <= 10) return ShaderType::PlanarYUV10;
    if (bitDepth <= 12) return ShaderType::PlanarYUV12;
    return ShaderType::PlanarYUV16;
}

ShaderType CapabilityRenderer::selectSemiPlanarShader(int bitDepth) const
{
    if (bitDepth <= 8) return ShaderType::SemiPlanar8;
    if (bitDepth <= 10) return ShaderType::SemiPlanar10;
    return ShaderType::SemiPlanar16;
}

void CapabilityRenderer::uploadPlanarYUV(const AVFrame *frame, const FrameInfo &info)
{
    int w = frame->width;
    int h = frame->height;
    int uw = w >> info.chromaWidthShift;
    int uh = h >> info.chromaHeightShift;

    GLenum type = info.isHighBitDepth() ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;
    GLint internalFmt = info.isHighBitDepth() ? GL_R16 : GL_R8;
    int bytesPerSample = info.isHighBitDepth() ? 2 : 1;

    m_uploader->uploadPlane(m_slotY, w, h, internalFmt, GL_RED, type,
                            frame->linesize[0] / bytesPerSample,
                            frame->data[0]);
    m_uploader->uploadPlane(m_slotU, uw, uh, internalFmt, GL_RED, type,
                            frame->linesize[1] / bytesPerSample,
                            frame->data[1]);
    m_uploader->uploadPlane(m_slotV, uw, uh, internalFmt, GL_RED, type,
                            frame->linesize[2] / bytesPerSample,
                            frame->data[2]);

    m_currentInfo.width = w;
    m_currentInfo.height = h;
}

void CapabilityRenderer::uploadSemiPlanar(const AVFrame *frame, const FrameInfo &info)
{
    int w = frame->width;
    int h = frame->height;
    int uw = w >> info.chromaWidthShift;
    int uh = h >> info.chromaHeightShift;

    GLenum type = info.isHighBitDepth() ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;
    GLint lumaFmt = info.isHighBitDepth() ? GL_R16 : GL_R8;
    GLint chromaFmt = info.isHighBitDepth() ? GL_RG16 : GL_RG8;
    int bytesPerSample = info.isHighBitDepth() ? 2 : 1;

    m_uploader->uploadPlane(m_slotY, w, h, lumaFmt, GL_RED, type,
                            frame->linesize[0] / bytesPerSample,
                            frame->data[0]);
    m_uploader->uploadPlane(m_slotUV, uw, uh, chromaFmt, GL_RG, type,
                            frame->linesize[1] / bytesPerSample,
                            frame->data[1]);

    m_currentInfo.width = w;
    m_currentInfo.height = h;
}

void CapabilityRenderer::uploadRGBFallback(const AVFrame *frame, const FrameInfo &info)
{
    Q_UNUSED(frame);
    Q_UNUSED(info);
}

void CapabilityRenderer::paint(QPainter &painter, const RenderState &state)
{
    if (!m_valid || !m_currentInfo.width || !m_currentInfo.height)
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

    painter.beginNativePainting();

    if (m_currentInfo.isSemiPlanar()) {
        ShaderType stype = selectSemiPlanarShader(m_currentInfo.bitDepth);
        ShaderProgram *sp = m_shaders->get(stype);
        if (sp && sp->program) {
            sp->program->bind();
            sp->program->setUniformValue(sp->uTexY, 0);
            sp->program->setUniformValue(sp->uTexUV, 1);

            m_vao->bind();
            glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

            m_uploader->bindToUnit(0, m_slotY);
            m_uploader->bindToUnit(1, m_slotUV);

            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

            m_vao->release();
            sp->program->release();
        }
    } else if (m_currentInfo.isPlanarYUV()) {
        ShaderType stype = selectPlanarShader(m_currentInfo.bitDepth);
        ShaderProgram *sp = m_shaders->get(stype);
        if (sp && sp->program) {
            sp->program->bind();
            sp->program->setUniformValue(sp->uTexY, 0);
            sp->program->setUniformValue(sp->uTexU, 1);
            sp->program->setUniformValue(sp->uTexV, 2);

            m_vao->bind();
            glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

            m_uploader->bindToUnit(0, m_slotY);
            m_uploader->bindToUnit(1, m_slotU);
            m_uploader->bindToUnit(2, m_slotV);

            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

            m_vao->release();
            sp->program->release();
        }
    }

    painter.endNativePainting();
}

void CapabilityRenderer::resize(int w, int h)
{
    m_viewportW = w;
    m_viewportH = h;
}

void CapabilityRenderer::cleanup()
{
    auto *ctx = QOpenGLContext::currentContext();
    if (ctx) {
        if (m_slotY.texId) { glDeleteTextures(1, &m_slotY.texId); m_slotY.texId = 0; }
        if (m_slotU.texId) { glDeleteTextures(1, &m_slotU.texId); m_slotU.texId = 0; }
        if (m_slotV.texId) { glDeleteTextures(1, &m_slotV.texId); m_slotV.texId = 0; }
        if (m_slotUV.texId) { glDeleteTextures(1, &m_slotUV.texId); m_slotUV.texId = 0; }
        if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
        if (m_vao) { m_vao->destroy(); delete m_vao; m_vao = nullptr; }
    }
    delete m_shaders;
    m_shaders = nullptr;
    delete m_uploader;
    m_uploader = nullptr;
    m_valid = false;
    m_currentInfo = FrameInfo{};
}

bool CapabilityRenderer::isValid() const
{
    return m_valid;
}
