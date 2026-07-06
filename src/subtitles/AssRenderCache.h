#ifndef ASSRENDERCACHE_H
#define ASSRENDERCACHE_H

#include <QHash>
#include <QList>
#include <QMutex>
#include <QFont>
#include <QPainterPath>
#include <QTransform>
#include <cstdint>
#include <QString>

class AssRenderCache {
public:
    explicit AssRenderCache(int64_t budgetBytes = 64LL * 1024 * 1024);
    ~AssRenderCache();

    // Glyph/text path cache: key = fontKey + "|" + text
    QPainterPath getTextPath(const QFont &font, const QString &text);
    void insertTextPath(const QFont &font, const QString &text, const QPainterPath &path);

    // Transform cache: key = rotation angles + scale
    QTransform getTransform(double rotX, double rotY, double rotZ,
                            double scaleX, double scaleY);
    void insertTransform(double rotX, double rotY, double rotZ,
                         double scaleX, double scaleY, const QTransform &xf);

    // Budget management
    void setBudget(int64_t bytes);
    int64_t budget() const { return m_budgetBytes; }
    int64_t used() const { return m_usedBytes; }
    void clear();

    // Check if a subtitle text has animation tags (to bypass surface cache)
    static bool hasAnimationTags(const QString &assText);

private:
    struct CacheEntry {
        int64_t size = 0;
        int64_t lastAccess = 0;
    };

    QString fontCacheKey(const QFont &font) const;
    int64_t pathSize(const QPainterPath &path) const;
    void evictOne();

    int64_t m_budgetBytes;
    mutable int64_t m_usedBytes = 0;
    mutable int64_t m_accessCounter = 0;
    mutable QMutex m_mutex;

    QHash<QString, QPainterPath> m_textPathCache;
    QHash<QString, CacheEntry> m_textPathMeta;
    QList<QString> m_textPathOrder;

    QHash<QString, QTransform> m_transformCache;
    QHash<QString, CacheEntry> m_transformMeta;
    QList<QString> m_transformOrder;
};

#endif
