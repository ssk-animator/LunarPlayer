#include "SubtitleSurface.h"

SubtitleSurface::SubtitleSurface() = default;

SubtitleSurface::SubtitleSurface(const QImage &image, int posX, int posY)
    : m_image(image)
    , m_posX(posX)
    , m_posY(posY)
{
}

void SubtitleSurface::upload()
{
    // CPU mode: no-op. Phase 11 overrides for GPU texture upload.
}

int64_t SubtitleSurface::memoryBytes() const
{
    if (m_image.isNull()) return 0;
    return static_cast<int64_t>(m_image.width()) * m_image.height() * 4;
}
