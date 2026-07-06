#ifndef TEXTUREUPLOADER_H
#define TEXTUREUPLOADER_H

#include <QOpenGLFunctions>

struct TextureSlot {
    GLuint texId = 0;
    int width = 0;
    int height = 0;
    int internalFormat = 0;
    int pixelFormat = 0;
    int dataType = 0;
};

class TextureUploader {
public:
    explicit TextureUploader(QOpenGLFunctions *gl) : m_gl(gl) {}
    ~TextureUploader() = default;

    void allocateTextures(TextureSlot &slot, int w, int h,
                          int internalFmt, int pixFmt, int dataType);

    void uploadPlane(TextureSlot &slot, int w, int h,
                     int internalFmt, int pixFmt, int dataType,
                     int rowLengthPixels, const void *data);

    void bindToUnit(int unit, const TextureSlot &slot);
    void setLinearFilter(TextureSlot &slot);

    void release();

private:
    QOpenGLFunctions *m_gl = nullptr;
};

#endif
