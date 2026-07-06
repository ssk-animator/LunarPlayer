#include "TextureUploader.h"

void TextureUploader::allocateTextures(TextureSlot &slot, int w, int h,
                                       int internalFmt, int pixFmt, int dataType)
{
    if (slot.texId == 0) {
        m_gl->glGenTextures(1, &slot.texId);
    }

    m_gl->glBindTexture(GL_TEXTURE_2D, slot.texId);

    bool needsRealloc = (w != slot.width || h != slot.height ||
                         internalFmt != slot.internalFormat);

    if (needsRealloc) {
        m_gl->glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0,
                           pixFmt, dataType, nullptr);
        slot.width = w;
        slot.height = h;
        slot.internalFormat = internalFmt;
        slot.pixelFormat = pixFmt;
        slot.dataType = dataType;
    }

    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void TextureUploader::uploadPlane(TextureSlot &slot, int w, int h,
                                  int internalFmt, int pixFmt, int dataType,
                                  int rowLengthPixels, const void *data)
{
    allocateTextures(slot, w, h, internalFmt, pixFmt, dataType);

    m_gl->glBindTexture(GL_TEXTURE_2D, slot.texId);

    int bytesPerPixel = 1;
    if (dataType == GL_UNSIGNED_SHORT) bytesPerPixel = 2;
    else if (dataType == GL_FLOAT) bytesPerPixel = 4;

    int alignment = bytesPerPixel;
    if (alignment > 4) alignment = 4;
    m_gl->glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
    m_gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLengthPixels);

    m_gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                          pixFmt, dataType, data);

    m_gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    m_gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void TextureUploader::bindToUnit(int unit, const TextureSlot &slot)
{
    m_gl->glActiveTexture(GL_TEXTURE0 + unit);
    m_gl->glBindTexture(GL_TEXTURE_2D, slot.texId);
}

void TextureUploader::setLinearFilter(TextureSlot &slot)
{
    m_gl->glBindTexture(GL_TEXTURE_2D, slot.texId);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void TextureUploader::release()
{
    m_gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    m_gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}
