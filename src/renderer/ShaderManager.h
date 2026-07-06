#ifndef SHADERMANAGER_H
#define SHADERMANAGER_H

#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QMap>

enum class ShaderType {
    PlanarYUV8,
    PlanarYUV10,
    PlanarYUV12,
    PlanarYUV16,
    SemiPlanar8,
    SemiPlanar10,
    SemiPlanar16,
    RGB8,
};

struct ShaderProgram {
    QOpenGLShaderProgram *program = nullptr;
    GLint uTexY = -1;
    GLint uTexU = -1;
    GLint uTexV = -1;
    GLint uTexUV = -1;
    GLint uBitDepth = -1;
};

class ShaderManager {
public:
    ShaderManager() = default;
    ~ShaderManager();

    void initialize(QOpenGLFunctions *gl);

    ShaderProgram *get(ShaderType type);

private:
    void createPlanarShader(ShaderType type, const QByteArray &version,
                            const QByteArray &precision, bool is10bit);
    void createSemiPlanarShader(ShaderType type, const QByteArray &version,
                                const QByteArray &precision, bool is10bit);
    void createRGBShader(const QByteArray &version, const QByteArray &precision);

    QOpenGLFunctions *m_gl = nullptr;
    QMap<ShaderType, ShaderProgram*> m_shaders;
};

#endif
