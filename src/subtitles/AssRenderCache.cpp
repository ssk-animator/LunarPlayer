#include "AssRenderCache.h"
#include <QRegularExpression>
#include <algorithm>

AssRenderCache::AssRenderCache(int64_t budgetBytes)
    : m_budgetBytes(budgetBytes)
{
}

AssRenderCache::~AssRenderCache() = default;

QString AssRenderCache::fontCacheKey(const QFont &font) const
{
    return font.family() + "|" + QString::number(font.pointSizeF())
         + "|" + (font.bold() ? "b" : "")
         + "|" + (font.italic() ? "i" : "")
         + "|" + QString::number(font.letterSpacing());
}

int64_t AssRenderCache::pathSize(const QPainterPath &path) const
{
    // Estimate: each element ~40 bytes
    return static_cast<int64_t>(path.elementCount()) * 40 + 64;
}

QPainterPath AssRenderCache::getTextPath(const QFont &font, const QString &text)
{
    QMutexLocker lock(&m_mutex);
    QString key = fontCacheKey(font) + "|" + text;
    auto it = m_textPathCache.find(key);
    if (it != m_textPathCache.end()) {
        m_textPathOrder.removeOne(key);
        m_textPathOrder.prepend(key);
        m_textPathMeta[key].lastAccess = ++m_accessCounter;
        return it.value();
    }
    return QPainterPath();
}

void AssRenderCache::insertTextPath(const QFont &font, const QString &text,
                                     const QPainterPath &path)
{
    QMutexLocker lock(&m_mutex);
    QString key = fontCacheKey(font) + "|" + text;
    int64_t size = pathSize(path);

    if (m_textPathCache.contains(key)) {
        m_usedBytes -= m_textPathMeta[key].size;
        m_usedBytes += size;
        m_textPathCache[key] = path;
        m_textPathMeta[key] = {size, ++m_accessCounter};
        m_textPathOrder.removeOne(key);
        m_textPathOrder.prepend(key);
        return;
    }

    if (m_budgetBytes > 0) {
        while (m_usedBytes + size > m_budgetBytes && !m_textPathCache.isEmpty())
            evictOne();
    }

    m_textPathCache[key] = path;
    m_textPathMeta[key] = {size, ++m_accessCounter};
    m_textPathOrder.prepend(key);
    m_usedBytes += size;
}

QTransform AssRenderCache::getTransform(double rotX, double rotY, double rotZ,
                                         double scaleX, double scaleY)
{
    QMutexLocker lock(&m_mutex);
    QString key = QString::number(rotX, 'f', 2) + "|"
                + QString::number(rotY, 'f', 2) + "|"
                + QString::number(rotZ, 'f', 2) + "|"
                + QString::number(scaleX, 'f', 4) + "|"
                + QString::number(scaleY, 'f', 4);
    auto it = m_transformCache.find(key);
    if (it != m_transformCache.end()) {
        m_transformOrder.removeOne(key);
        m_transformOrder.prepend(key);
        m_transformMeta[key].lastAccess = ++m_accessCounter;
        return it.value();
    }
    return QTransform();
}

void AssRenderCache::insertTransform(double rotX, double rotY, double rotZ,
                                      double scaleX, double scaleY,
                                      const QTransform &xf)
{
    QMutexLocker lock(&m_mutex);
    QString key = QString::number(rotX, 'f', 2) + "|"
                + QString::number(rotY, 'f', 2) + "|"
                + QString::number(rotZ, 'f', 2) + "|"
                + QString::number(scaleX, 'f', 4) + "|"
                + QString::number(scaleY, 'f', 4);
    int64_t size = 128; // fixed estimate per transform

    if (m_transformCache.contains(key)) {
        m_transformCache[key] = xf;
        m_transformMeta[key] = {size, ++m_accessCounter};
        m_transformOrder.removeOne(key);
        m_transformOrder.prepend(key);
        return;
    }

    if (m_budgetBytes > 0) {
        while (m_usedBytes + size > m_budgetBytes && !m_transformCache.isEmpty())
            evictOne();
    }

    m_transformCache[key] = xf;
    m_transformMeta[key] = {size, ++m_accessCounter};
    m_transformOrder.prepend(key);
    m_usedBytes += size;
}

void AssRenderCache::setBudget(int64_t bytes)
{
    QMutexLocker lock(&m_mutex);
    m_budgetBytes = bytes;
    if (m_budgetBytes > 0) {
        while (m_usedBytes > m_budgetBytes && !m_textPathCache.isEmpty())
            evictOne();
    }
}

void AssRenderCache::clear()
{
    QMutexLocker lock(&m_mutex);
    m_textPathCache.clear();
    m_textPathMeta.clear();
    m_textPathOrder.clear();
    m_transformCache.clear();
    m_transformMeta.clear();
    m_transformOrder.clear();
    m_usedBytes = 0;
}

void AssRenderCache::evictOne()
{
    if (!m_textPathOrder.isEmpty()) {
        QString last = m_textPathOrder.last();
        auto it = m_textPathCache.find(last);
        if (it != m_textPathCache.end()) {
            m_usedBytes -= m_textPathMeta[last].size;
            m_textPathCache.erase(it);
            m_textPathMeta.remove(last);
        }
        m_textPathOrder.removeLast();
        return;
    }
    if (!m_transformOrder.isEmpty()) {
        QString last = m_transformOrder.last();
        auto it = m_transformCache.find(last);
        if (it != m_transformCache.end()) {
            m_usedBytes -= m_transformMeta[last].size;
            m_transformCache.erase(it);
            m_transformMeta.remove(last);
        }
        m_transformOrder.removeLast();
    }
}

bool AssRenderCache::hasAnimationTags(const QString &assText)
{
    static const QRegularExpression animRe(
        QStringLiteral("\\\\move|\\\\t\\(|\\\\fad|\\\\fade"));
    return assText.contains(animRe);
}
