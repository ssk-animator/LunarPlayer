#ifndef SUBTITLECACHE_H
#define SUBTITLECACHE_H

#include "SubtitleSurface.h"
#include <QHash>
#include <QList>
#include <QMutex>
#include <cstdint>

class SubtitleCache {
public:
    // Default budget: 128 MB
    explicit SubtitleCache(int64_t budgetBytes = 128LL * 1024 * 1024);
    ~SubtitleCache();

    void insert(uint64_t key, const SubtitleSurface &surface);
    bool lookup(uint64_t key, SubtitleSurface &out) const;
    void remove(uint64_t key);
    void clear();

    int size() const { return m_cache.size(); }
    int64_t usedBytes() const { return m_usedBytes; }
    int64_t budgetBytes() const { return m_budgetBytes; }
    void setBudgetBytes(int64_t bytes);

private:
    void evictOne();

    int64_t m_budgetBytes;
    mutable int64_t m_usedBytes = 0;
    mutable QMutex m_mutex;
    mutable QList<uint64_t> m_order;
    mutable QHash<uint64_t, SubtitleSurface> m_cache;
};

#endif // SUBTITLECACHE_H
