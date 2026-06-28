#ifndef GLVIDEORENDERER_H
#define GLVIDEORENDERER_H

#include "Renderer.h"
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>

class GLVideoRenderer : public Renderer, protected QOpenGLExtraFunctions {
public:
    GLVideoRenderer() = default;
    ~GLVideoRenderer() override;

    bool initialize() override;
    void present(const QImage &frame) override;
    void paint(QPainter &painter, const QRect &outputRect) override;
    void resize(int w, int h) override;
    void cleanup() override;
    bool isValid() const override;

private:
    bool m_valid = false;
    QImage m_frame;
    GLuint m_texture = 0;
    QOpenGLShaderProgram *m_program = nullptr;
    QOpenGLVertexArrayObject *m_vao = nullptr;
    GLuint m_vbo = 0;
    int m_texUniform = -1;
    int m_viewportW = 1;
    int m_viewportH = 1;
};

#endif
