#include "GPUTextureCache.h"
#include <QCryptographicHash>
#include <algorithm>

GPUTextureCache::GPUTextureCache(QObject *parent) : QObject(parent) {}

GPUTextureCache::~GPUTextureCache()
{
    clear();
}

bool GPUTextureCache::initialize()
{
    if (m_initialized) return true;
    initializeOpenGLFunctions();
    m_initialized = true;
    return true;
}

int64_t GPUTextureCache::imageSize(const QImage &img)
{
    return static_cast<int64_t>(img.width()) * img.height() * 4;
}

QString GPUTextureCache::imageCacheKey(const QImage &img)
{
    int w = img.width();
    int h = img.height();
    QByteArray data;
    data.append(reinterpret_cast<const char*>(&w), sizeof(int));
    data.append(reinterpret_cast<const char*>(&h), sizeof(int));
    int64_t copyBytes = std::min(static_cast<int64_t>(img.sizeInBytes()), static_cast<int64_t>(4096));
    data.append(reinterpret_cast<const char*>(img.constBits()), static_cast<int>(copyBytes));
    return QString::fromUtf8(QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex());
}

GLuint GPUTextureCache::uploadTexture(const QImage &image, const QString &key)
{
    QMutexLocker lock(&m_mutex);

    QString cacheKey = key.isEmpty() ? imageCacheKey(image) : key;

    auto it = m_textures.find(cacheKey);
    if (it != m_textures.end()) {
        it.value().lastAccess = ++m_accessCounter;
        return it.value().textureId;
    }

    int64_t size = imageSize(image);
    if (m_budgetBytes > 0) {
        while (m_usedBytes + size > m_budgetBytes && !m_textures.isEmpty())
            evictOne();
    }

    QImage img = image.convertToFormat(QImage::Format_RGBA8888);

    GLuint texId;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());

    TextureEntry entry;
    entry.textureId = texId;
    entry.size = size;
    entry.lastAccess = ++m_accessCounter;
    entry.width = img.width();
    entry.height = img.height();
    m_textures[cacheKey] = entry;
    m_usedBytes += size;

    emit textureUploaded(cacheKey, size);
    return texId;
}

GLuint GPUTextureCache::getTexture(const QString &key) const
{
    QMutexLocker lock(&m_mutex);
    auto it = m_textures.find(key);
    if (it != m_textures.end()) {
        const_cast<GPUTextureCache*>(this)->m_accessCounter++;
        return it.value().textureId;
    }
    return 0;
}

bool GPUTextureCache::hasTexture(const QString &key) const
{
    QMutexLocker lock(&m_mutex);
    return m_textures.contains(key);
}

void GPUTextureCache::removeTexture(const QString &key)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_textures.find(key);
    if (it != m_textures.end()) {
        glDeleteTextures(1, &it.value().textureId);
        m_usedBytes -= it.value().size;
        emit textureEvicted(key, it.value().size);
        m_textures.erase(it);
    }
}

GLuint GPUTextureCache::uploadSubtitle(const QImage &image, int64_t subtitleKey)
{
    QMutexLocker lock(&m_mutex);

    auto it = m_subtitleTextures.find(subtitleKey);
    if (it != m_subtitleTextures.end()) {
        it.value().lastAccess = ++m_accessCounter;
        return it.value().textureId;
    }

    int64_t size = imageSize(image);
    if (m_budgetBytes > 0) {
        while (m_usedBytes + size > m_budgetBytes && !m_subtitleTextures.isEmpty())
            evictSubtitleOne();
    }

    QImage img = image.convertToFormat(QImage::Format_RGBA8888);

    GLuint texId;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());

    TextureEntry entry;
    entry.textureId = texId;
    entry.size = size;
    entry.lastAccess = ++m_accessCounter;
    entry.width = img.width();
    entry.height = img.height();
    m_subtitleTextures[subtitleKey] = entry;
    m_usedBytes += size;

    return texId;
}

GLuint GPUTextureCache::getSubtitleTexture(int64_t subtitleKey) const
{
    QMutexLocker lock(&m_mutex);
    auto it = m_subtitleTextures.find(subtitleKey);
    if (it != m_subtitleTextures.end()) {
        const_cast<GPUTextureCache*>(this)->m_accessCounter++;
        return it.value().textureId;
    }
    return 0;
}

void GPUTextureCache::clear()
{
    QMutexLocker lock(&m_mutex);
    for (auto &entry : m_textures)
        glDeleteTextures(1, &entry.textureId);
    for (auto &entry : m_subtitleTextures)
        glDeleteTextures(1, &entry.textureId);
    m_textures.clear();
    m_subtitleTextures.clear();
    m_usedBytes = 0;
}

void GPUTextureCache::preserveTextures(const QVector<QString> &keys)
{
    QMutexLocker lock(&m_mutex);
    for (const auto &key : keys) {
        auto it = m_textures.find(key);
        if (it != m_textures.end())
            it.value().lastAccess = ++m_accessCounter;
    }
}

void GPUTextureCache::preserveSubtitleTextures(const QVector<int64_t> &keys)
{
    QMutexLocker lock(&m_mutex);
    for (int64_t key : keys) {
        auto it = m_subtitleTextures.find(key);
        if (it != m_subtitleTextures.end())
            it.value().lastAccess = ++m_accessCounter;
    }
}

void GPUTextureCache::purge()
{
    QMutexLocker lock(&m_mutex);
    int64_t target = static_cast<int64_t>(m_budgetBytes * 0.8);
    while (m_usedBytes > target && m_textures.size() > 1)
        evictOne();
    while (m_usedBytes > target && m_subtitleTextures.size() > 1)
        evictSubtitleOne();
}

void GPUTextureCache::evictOne()
{
    if (m_textures.isEmpty()) return;

    QString lruKey;
    uint64_t oldest = UINT64_MAX;
    for (auto it = m_textures.constBegin(); it != m_textures.constEnd(); ++it) {
        if (it.value().lastAccess < oldest) {
            oldest = it.value().lastAccess;
            lruKey = it.key();
        }
    }

    if (!lruKey.isEmpty()) {
        auto entryIt = m_textures.find(lruKey);
        int64_t evictedSize = entryIt.value().size;
        glDeleteTextures(1, &entryIt.value().textureId);
        m_usedBytes -= evictedSize;
        emit textureEvicted(lruKey, evictedSize);
        m_textures.erase(entryIt);
    }
}

void GPUTextureCache::evictSubtitleOne()
{
    if (m_subtitleTextures.isEmpty()) return;

    int64_t lruKey = 0;
    uint64_t oldest = UINT64_MAX;
    for (auto it = m_subtitleTextures.constBegin(); it != m_subtitleTextures.constEnd(); ++it) {
        if (it.value().lastAccess < oldest) {
            oldest = it.value().lastAccess;
            lruKey = it.key();
        }
    }

    if (lruKey != 0) {
        auto entryIt = m_subtitleTextures.find(lruKey);
        glDeleteTextures(1, &entryIt.value().textureId);
        m_usedBytes -= entryIt.value().size;
        m_subtitleTextures.erase(entryIt);
    }
}
