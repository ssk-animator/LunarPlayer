#ifndef CROPANALYZER_H
#define CROPANALYZER_H

#include <QObject>
#include <QImage>
#include <QSize>

class CropAnalyzer : public QObject
{
    Q_OBJECT
public:
    explicit CropAnalyzer(QObject *parent = nullptr);

    // Analyze frame for black bars, returns normalized source rect (0..1)
    // Empty rect means no cropping needed
    QRectF detectBlackBars(const QImage &frame);

    // Cache keyed on frame dimensions + a content hash for scene change detection
    void invalidate();
    bool isValid() const { return m_cached; }

private:
    int scanEdge(const QImage &frame, Qt::Edge edge, int threshold) const;

    bool m_cached = false;
    QRectF m_cachedRect;
    int m_cachedW = 0;
    int m_cachedH = 0;
};

#endif
