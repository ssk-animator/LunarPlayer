#include "ShaderManager.h"

static const char *kVertexSrc =
    "layout(location = 0) in vec2 aPos;\n"
    "layout(location = 1) in vec2 aTexCoord;\n"
    "out vec2 vTexCoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "    vTexCoord = aTexCoord;\n"
    "}\n";

static const char *kPlanarFragSrc8 =
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

static const char *kPlanarFragSrc10 =
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

static const char *kSemiPlanarFragSrc8 =
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

static const char *kSemiPlanarFragSrc10 =
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

static const char *kRGBFragSrc =
    "in vec2 vTexCoord;\n"
    "out vec4 fragColor;\n"
    "uniform sampler2D uTexRGB;\n"
    "void main() {\n"
    "    fragColor = texture(uTexRGB, vTexCoord);\n"
    "}\n";

ShaderManager::~ShaderManager()
{
    for (auto *s : m_shaders) {
        if (s->program) delete s->program;
        delete s;
    }
    m_shaders.clear();
}

void ShaderManager::initialize(QOpenGLFunctions *gl)
{
    m_gl = gl;

    auto *ctx = QOpenGLContext::currentContext();
    QByteArray ver, prec;
    if (ctx->isOpenGLES()) {
        ver = "#version 300 es\n";
        prec = "precision mediump float;\n";
    } else {
        ver = "#version 330 core\n";
        prec = "";
    }

    createPlanarShader(ShaderType::PlanarYUV8, ver, prec, false);
    createPlanarShader(ShaderType::PlanarYUV10, ver, prec, true);
    createPlanarShader(ShaderType::PlanarYUV12, ver, prec, true);
    createPlanarShader(ShaderType::PlanarYUV16, ver, prec, true);
    createSemiPlanarShader(ShaderType::SemiPlanar8, ver, prec, false);
    createSemiPlanarShader(ShaderType::SemiPlanar10, ver, prec, true);
    createSemiPlanarShader(ShaderType::SemiPlanar16, ver, prec, true);
    createRGBShader(ver, prec);
}

ShaderProgram *ShaderManager::get(ShaderType type)
{
    return m_shaders.value(type, nullptr);
}

void ShaderManager::createPlanarShader(ShaderType type, const QByteArray &version,
                                       const QByteArray &precision, bool is10bit)
{
    auto *prog = new QOpenGLShaderProgram();
    if (!prog->addShaderFromSourceCode(QOpenGLShader::Vertex, version + kVertexSrc) ||
        !prog->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                        version + precision +
                                        (is10bit ? kPlanarFragSrc10 : kPlanarFragSrc8)) ||
        !prog->link()) {
        delete prog;
        return;
    }

    auto *sp = new ShaderProgram();
    sp->program = prog;
    sp->uTexY = prog->uniformLocation("uTexY");
    sp->uTexU = prog->uniformLocation("uTexU");
    sp->uTexV = prog->uniformLocation("uTexV");
    m_shaders[type] = sp;
}

void ShaderManager::createSemiPlanarShader(ShaderType type, const QByteArray &version,
                                           const QByteArray &precision, bool is10bit)
{
    auto *prog = new QOpenGLShaderProgram();
    if (!prog->addShaderFromSourceCode(QOpenGLShader::Vertex, version + kVertexSrc) ||
        !prog->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                        version + precision +
                                        (is10bit ? kSemiPlanarFragSrc10 : kSemiPlanarFragSrc8)) ||
        !prog->link()) {
        delete prog;
        return;
    }

    auto *sp = new ShaderProgram();
    sp->program = prog;
    sp->uTexY = prog->uniformLocation("uTexY");
    sp->uTexUV = prog->uniformLocation("uTexUV");
    m_shaders[type] = sp;
}

void ShaderManager::createRGBShader(const QByteArray &version, const QByteArray &precision)
{
    auto *prog = new QOpenGLShaderProgram();
    if (!prog->addShaderFromSourceCode(QOpenGLShader::Vertex, version + kVertexSrc) ||
        !prog->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                        version + precision + kRGBFragSrc) ||
        !prog->link()) {
        delete prog;
        return;
    }

    auto *sp = new ShaderProgram();
    sp->program = prog;
    sp->uTexY = prog->uniformLocation("uTexRGB");
    m_shaders[ShaderType::RGB8] = sp;
}
