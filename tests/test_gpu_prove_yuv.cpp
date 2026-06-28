// Phase 2C GPU YUV Prove Test
// Validates that YUVVideoRenderer correctly converts YUV420P to RGB via GPU shader.
// Creates synthetic YUV420P frame (Y=128, U=128, V=128 = medium gray),
// renders through YUVVideoRenderer to FBO, and verifies output ≈ 128 per channel.
// Run: build\LunarPlayerGPUProveYUV.exe (requires native display)

#include <QApplication>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QPainter>
#include <cstdio>
#include "renderer/YUVVideoRenderer.h"

extern "C" {
#include <libavutil/frame.h>
}

class YUVProveWidget : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    YUVVideoRenderer *renderer = nullptr;
    AVFrame *testFrame = nullptr;

    YUVProveWidget() { resize(320, 240); }

protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        renderer = new YUVVideoRenderer();
        if (!renderer->initialize()) {
            printf("YUV_PROVE: renderer init FAILED\n"); return;
        }

        testFrame = av_frame_alloc();
        testFrame->format = AV_PIX_FMT_YUV420P;
        testFrame->width = 320;
        testFrame->height = 240;
        av_frame_get_buffer(testFrame, 0);

        // Fill with medium gray: Y=128, U=128, V=128
        memset(testFrame->data[0], 128, testFrame->linesize[0] * 240);
        memset(testFrame->data[1], 128, testFrame->linesize[1] * 120);
        memset(testFrame->data[2], 128, testFrame->linesize[2] * 120);

        renderer->presentAVFrame(testFrame);
        printf("YUV_PROVE: uploaded %dx%d\n", testFrame->width, testFrame->height);
        update();
    }

    void paintGL() override {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (!renderer || !renderer->isValid()) return;
        QPainter p(this);
        renderer->paint(p, QRect(0, 0, 320, 240));
    }

    void resizeGL(int w, int h) override {
        glViewport(0, 0, w, h);
        if (renderer) renderer->resize(w, h);
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    YUVProveWidget w;
    w.show();
    for (int i = 0; i < 10; i++) QApplication::processEvents();

    QImage grab = w.grabFramebuffer();
    if (grab.isNull()) { printf("YUV_PROVE FAIL: null grab\n"); return 1; }

    double rSum = 0, gSum = 0, bSum = 0;
    int cnt = 0;
    for (int y = 0; y < qMin(grab.height(), 32); y++) {
        for (int x = 0; x < qMin(grab.width(), 32); x++) {
            QRgb px = grab.pixel(x, y);
            rSum += qRed(px); gSum += qGreen(px); bSum += qBlue(px);
            cnt++;
        }
    }
    double rAvg = rSum / cnt, gAvg = gSum / cnt, bAvg = bSum / cnt;
    double avg = (rAvg + gAvg + bAvg) / 3.0;
    printf("YUV_PROVE: R=%.1f G=%.1f B=%.1f avg=%.2f\n", rAvg, gAvg, bAvg, avg);

    // Gray (Y=128, U=128, V=128) should produce R≈G≈B≈128
    bool pass = (rAvg > 110 && rAvg < 150) &&
                (gAvg > 110 && gAvg < 150) &&
                (bAvg > 110 && bAvg < 150);
    printf("YUV_PROVE: %s\n", pass ? "PASS" : "FAIL");
    printf("DONE %s\n", pass ? "PASS" : "FAIL");

    av_frame_free(&w.testFrame);
    return pass ? 0 : 1;
}
