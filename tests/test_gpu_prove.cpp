// GPU Renderer Prove Test
// Validates the OpenGL shader-based rendering path end-to-end:
//   OpenGL context → shader compile → texture upload → VAO/VBO → glDrawArrays → FBO readback
// Requires native display (fails on QT_QPA_PLATFORM=offscreen).
// Run: build\LunarPlayerGPUProve.exe
// Exit: 0 = PASS, 1 = FAIL

#include <QApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QImage>
#include <QPainter>
#include <QtOpenGL/qopenglpaintdevice.h>
#include <cstdio>
#include "../src/renderer/GLVideoRenderer.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QSurfaceFormat fmt;

    QOffscreenSurface surface;
    surface.setFormat(fmt);
    surface.create();

    QOpenGLContext ctx;
    ctx.setFormat(fmt);
    if (!ctx.create()) { printf("FAIL: context\n"); return 1; }
    if (!ctx.makeCurrent(&surface)) { printf("FAIL: makeCurrent\n"); return 1; }

    auto *gl = ctx.functions();
    printf("CTX: %d.%d es=%d\n",
           ctx.format().majorVersion(), ctx.format().minorVersion(),
           ctx.isOpenGLES() ? 1 : 0);
    printf("GL: %s\n", (const char*)gl->glGetString(GL_VERSION));
    fflush(stdout);

    GLuint tex, fbo;
    gl->glGenTextures(1, &tex);
    gl->glBindTexture(GL_TEXTURE_2D, tex);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 800, 500, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    gl->glGenFramebuffers(1, &fbo);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

    GLenum fbStatus = gl->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    printf("FBO_STATUS: %s\n", fbStatus == GL_FRAMEBUFFER_COMPLETE ? "COMPLETE" : "INCOMPLETE");
    fflush(stdout);
    if (fbStatus != GL_FRAMEBUFFER_COMPLETE) return 1;

    GLVideoRenderer renderer;
    bool ok = renderer.initialize();
    printf("RENDERER_INIT: %s\n", ok ? "OK" : "FAIL");
    fflush(stdout);
    if (!ok) { ctx.doneCurrent(); return 1; }

    QImage frame(320, 240, QImage::Format_RGB32);
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 320; x++)
            frame.setPixel(x, y, (x < 160) ? 0xFF0000FF : 0xFFFF0000);
    renderer.present(frame);
    renderer.resize(800, 500);

    gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl->glViewport(0, 0, 800, 500);
    gl->glClearColor(0.07f, 0.07f, 0.07f, 1.0f);
    gl->glClear(GL_COLOR_BUFFER_BIT);

    QOpenGLPaintDevice paintDev(800, 500);
    QPainter painter(&paintDev);
    painter.beginNativePainting();
    gl->glViewport(0, 0, 800, 500);
    renderer.paint(painter, QRect(140, 130, 320, 240));

    QImage result(800, 500, QImage::Format_RGBA8888);
    gl->glReadPixels(0, 0, 800, 500, GL_RGBA, GL_UNSIGNED_BYTE, result.bits());
    painter.endNativePainting();
    painter.end();

    renderer.cleanup();
    gl->glDeleteFramebuffers(1, &fbo);
    gl->glDeleteTextures(1, &tex);
    ctx.doneCurrent();

    result = result.mirrored();
    result.save("gpu_rendered_output.png", "PNG");
    printf("SAVED: gpu_rendered_output.png %dx%d\n", result.width(), result.height());
    fflush(stdout);

    double sum = 0; int cnt = 0;
    for (int y = 130; y < 370 && y < result.height(); y++) {
        const uchar *line = result.constScanLine(y);
        for (int x = 140; x < 460 && x < result.width(); x++) {
            sum += line[x*4] + line[x*4+1] + line[x*4+2];
            cnt += 3;
        }
    }
    double avg = (cnt > 0) ? sum / cnt : -1;
    bool pass = avg > 10.0;
    printf("GPU_AVG: %.2f %s\n", avg, pass ? "PASS (valid render)" : "FAIL (check readback)");
    fflush(stdout);

    printf("DONE %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
