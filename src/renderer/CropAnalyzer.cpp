#include "CropAnalyzer.h"
#include <algorithm>
#include <cmath>

CropAnalyzer::CropAnalyzer(QObject *parent)
    : QObject(parent)
{
}

void CropAnalyzer::invalidate()
{
    m_cached = false;
    m_cachedRect = QRectF();
}

QRectF CropAnalyzer::detectBlackBars(const QImage &frame)
{
    if (frame.isNull())
        return {};

    int w = frame.width();
    int h = frame.height();

    // Return cached result if dimensions match
    if (m_cached && m_cachedW == w && m_cachedH == h)
        return m_cachedRect;

    const int luminanceThreshold = 32;

    // Find black bar extents from each edge
    int cropTop = scanEdge(frame, Qt::TopEdge, luminanceThreshold);
    int cropBottom = scanEdge(frame, Qt::BottomEdge, luminanceThreshold);
    int cropLeft = scanEdge(frame, Qt::LeftEdge, luminanceThreshold);
    int cropRight = scanEdge(frame, Qt::RightEdge, luminanceThreshold);

    // Validate: don't crop too aggressively
    if (cropTop + cropBottom >= h - 4 || cropLeft + cropRight >= w - 4) {
        // Too much detected as black, likely a false positive
        m_cached = true;
        m_cachedW = w;
        m_cachedH = h;
        m_cachedRect = QRectF();
        return m_cachedRect;
    }

    // Compute normalized source rectangle
    if (cropTop == 0 && cropBottom == 0 && cropLeft == 0 && cropRight == 0) {
        m_cachedRect = QRectF();
    } else {
        float x0 = static_cast<float>(cropLeft) / w;
        float y0 = static_cast<float>(cropTop) / h;
        float x1 = static_cast<float>(w - cropRight) / w;
        float y1 = static_cast<float>(h - cropBottom) / h;
        m_cachedRect = QRectF(x0, y0, x1 - x0, y1 - y0);
    }

    m_cached = true;
    m_cachedW = w;
    m_cachedH = h;
    return m_cachedRect;
}

int CropAnalyzer::scanEdge(const QImage &frame, Qt::Edge edge, int threshold) const
{
    int w = frame.width();
    int h = frame.height();
    int step = qMax(1, (edge == Qt::TopEdge || edge == Qt::BottomEdge) ? w / 64 : h / 64);
    int maxScan = (edge == Qt::TopEdge || edge == Qt::BottomEdge) ? h / 4 : w / 4;
    int sampleRows = qMax(1, h / 100); // sample multiple rows for robustness
    int sampleCols = qMax(1, w / 100);

    int blackCount = 0;
    int totalSamples = 0;

    auto isDark = [&](int px) -> bool {
        int r = qRed(px), g = qGreen(px), b = qBlue(px);
        // Luminance: 0.299*R + 0.587*G + 0.114*B
        double lum = 0.299 * r + 0.587 * g + 0.114 * b;
        return lum < threshold;
    };

    switch (edge) {
    case Qt::TopEdge:
        for (int y = 0; y < maxScan && y < h; ++y) {
            int dark = 0;
            for (int x = 0; x < w; x += step) {
                if (isDark(frame.pixel(x, y))) ++dark;
            }
            int samp = w / step;
            if (dark * 100 / samp > 80)
                ++blackCount;
            else
                break; // stop at first non-black row
        }
        break;

    case Qt::BottomEdge:
        for (int y = h - 1; y >= h - maxScan && y >= 0; --y) {
            // Skip bottom subtitle area (ignore last 8% for subtitle avoidance)
            if (y > h - h / 12) continue;
            int dark = 0;
            for (int x = 0; x < w; x += step) {
                if (isDark(frame.pixel(x, y))) ++dark;
            }
            int samp = w / step;
            if (dark * 100 / samp > 80)
                ++blackCount;
            else
                break;
        }
        break;

    case Qt::LeftEdge:
        for (int x = 0; x < maxScan && x < w; ++x) {
            int dark = 0;
            for (int y = 0; y < h; y += step) {
                if (isDark(frame.pixel(x, y))) ++dark;
            }
            int samp = h / step;
            if (dark * 100 / samp > 80)
                ++blackCount;
            else
                break;
        }
        break;

    case Qt::RightEdge:
        for (int x = w - 1; x >= w - maxScan && x >= 0; --x) {
            int dark = 0;
            for (int y = 0; y < h; y += step) {
                if (isDark(frame.pixel(x, y))) ++dark;
            }
            int samp = h / step;
            if (dark * 100 / samp > 80)
                ++blackCount;
            else
                break;
        }
        break;
    }

    return blackCount;
}
