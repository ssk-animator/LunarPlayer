#pragma once
#include <QObject>
#include <QImage>
#include <QMap>
#include <QMutex>
#include <QOpenGLFunctions>
#include <QVector>
#include <QPair>
#include <cstdint>

class GPUTextureCache : public QObject, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit GPUTextureCache(QObject *parent = nullptr);
    ~GPUTextureCache() override;

    // Initialize OpenGL resources
    bool initialize();

    // Upload a QImage to GPU and return texture ID
    GLuint uploadTexture(const QImage &image, const QString &key = QString());

    // Retrieve a cached texture
    GLuint getTexture(const QString &key) const;

    // Check if a texture is cached
    bool hasTexture(const QString &key) const;

    // Remove a texture from the cache
    void removeTexture(const QString &key);

    // Upload and cache a subtitle image
    GLuint uploadSubtitle(const QImage &image, int64_t subtitleKey);

    // Get cached subtitle texture
    GLuint getSubtitleTexture(int64_t subtitleKey) const;

    // Clear all cached textures
    void clear();

    // Budget management
    void setBudgetBytes(int64_t bytes) { m_budgetBytes = bytes; }
    int64_t budgetBytes() const { return m_budgetBytes; }
    int64_t usedBytes() const { return m_usedBytes; }
    int textureCount() const { return m_textures.size(); }
    int subtitleTextureCount() const { return m_subtitleTextures.size(); }

    // Ensure a minimum set of textures survives the next purge
    void preserveTextures(const QVector<QString> &keys);
    void preserveSubtitleTextures(const QVector<int64_t> &keys);

    // Purge least-recently-used textures
    void purge();

signals:
    void textureUploaded(const QString &key, int64_t size);
    void textureEvicted(const QString &key, int64_t size);

private:
    struct TextureEntry {
        GLuint textureId = 0;
        int64_t size = 0;
        uint64_t lastAccess = 0;
        int width = 0;
        int height = 0;
    };

    // Estimate memory used by a QImage
    static int64_t imageSize(const QImage &img);

    // Generate a unique cache key for an image
    static QString imageCacheKey(const QImage &img);

    // Evict least-recently-used texture
    void evictOne();
    void evictSubtitleOne();

    QMap<QString, TextureEntry> m_textures;
    QMap<int64_t, TextureEntry> m_subtitleTextures;
    mutable QMutex m_mutex;

    int64_t m_budgetBytes = static_cast<int64_t>(256) * 1024 * 1024; // 256MB
    int64_t m_usedBytes = 0;
    uint64_t m_accessCounter = 0;
    bool m_initialized = false;
};
