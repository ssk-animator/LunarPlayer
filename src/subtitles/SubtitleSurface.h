#ifndef SUBTITLESURFACE_H
#define SUBTITLESURFACE_H

#include <QImage>
#include <QSize>
#include <cstdint>

// SubtitleSurface is the only rendering primitive the renderer understands.
// It wraps either a CPU-side QImage or a future GPU texture handle.
// The renderer never knows about AVSubtitle, FFmpeg, PGS, DVD, or DVB.
class SubtitleSurface {
public:
    enum Backend { CPU };

    SubtitleSurface();
    explicit SubtitleSurface(const QImage &image, int posX = 0, int posY = 0);

    bool isNull() const { return m_image.isNull(); }
    QImage toImage() const { return m_image; }
    QSize size() const { return m_image.size(); }
    int width() const { return m_image.width(); }
    int height() const { return m_image.height(); }
    int posX() const { return m_posX; }
    int posY() const { return m_posY; }
    Backend backend() const { return CPU; }

    // GPU upload stub — no-op in CPU mode, overridden in Phase 11
    virtual void upload();
    virtual quint64 textureId() const { return 0; }

    // Estimate memory usage in bytes (for cache budget tracking)
    int64_t memoryBytes() const;

private:
    QImage m_image;
    int m_posX = 0;
    int m_posY = 0;
};

#endif // SUBTITLESURFACE_H
