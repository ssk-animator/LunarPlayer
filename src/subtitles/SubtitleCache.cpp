#include "SubtitleCache.h"

SubtitleCache::SubtitleCache(int64_t budgetBytes)
    : m_budgetBytes(budgetBytes)
{
}

SubtitleCache::~SubtitleCache() = default;

void SubtitleCache::insert(uint64_t key, const SubtitleSurface &surface)
{
    QMutexLocker lock(&m_mutex);
    int64_t size = surface.memoryBytes();

    if (m_cache.contains(key)) {
        m_order.removeOne(key);
        m_usedBytes -= m_cache[key].memoryBytes();
        m_cache[key] = surface;
        m_usedBytes += size;
        m_order.prepend(key);
        return;
    }

    if (m_budgetBytes > 0) {
        while (m_usedBytes + size > m_budgetBytes && !m_cache.isEmpty())
            evictOne();
    }

    m_cache[key] = surface;
    m_usedBytes += size;
    m_order.prepend(key);
}

bool SubtitleCache::lookup(uint64_t key, SubtitleSurface &out) const
{
    QMutexLocker lock(&m_mutex);
    auto it = m_cache.find(key);
    if (it == m_cache.end())
        return false;
    m_order.removeOne(key);
    m_order.prepend(key);
    out = it.value();
    return true;
}

void SubtitleCache::remove(uint64_t key)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_cache.find(key);
    if (it == m_cache.end()) return;
    m_usedBytes -= it.value().memoryBytes();
    m_order.removeOne(key);
    m_cache.erase(it);
}

void SubtitleCache::clear()
{
    QMutexLocker lock(&m_mutex);
    m_cache.clear();
    m_order.clear();
    m_usedBytes = 0;
}

void SubtitleCache::setBudgetBytes(int64_t bytes)
{
    QMutexLocker lock(&m_mutex);
    m_budgetBytes = bytes;
    if (m_budgetBytes > 0) {
        while (m_usedBytes > m_budgetBytes && !m_cache.isEmpty())
            evictOne();
    }
}

void SubtitleCache::evictOne()
{
    if (m_order.isEmpty()) return;
    uint64_t last = m_order.last();
    auto it = m_cache.find(last);
    if (it != m_cache.end()) {
        m_usedBytes -= it.value().memoryBytes();
        m_cache.erase(it);
    }
    m_order.removeLast();
}
