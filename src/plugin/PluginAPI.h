#pragma once
#include <QString>
#include <QVector>
#include <QVariantMap>
#include <functional>

// Plugin versioning
#define LUNAR_PLUGIN_API_VERSION 1

// Plugin type flags
enum class PluginType : uint32_t {
    Decoder     = 1 << 0,
    Demuxer     = 1 << 1,
    Renderer    = 1 << 2,
    Network     = 1 << 3,
    Subtitle    = 1 << 4,
    MediaImport = 1 << 5,
    Metadata    = 1 << 6,
    RAW         = 1 << 7,
    Cloud       = 1 << 8,
    AI          = 1 << 9
};

inline PluginType operator|(PluginType a, PluginType b) {
    return static_cast<PluginType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline uint32_t operator&(PluginType a, PluginType b) {
    return static_cast<uint32_t>(a) & static_cast<uint32_t>(b);
}

struct PluginInfo {
    QString name;
    QString version;
    QString author;
    QString description;
    PluginType types;
    uint32_t apiVersion = LUNAR_PLUGIN_API_VERSION;
    QVariantMap capabilities;
};

class PluginBase
{
public:
    virtual ~PluginBase() = default;
    virtual PluginInfo info() const = 0;
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual bool isInitialized() const { return m_initialized; }

protected:
    bool m_initialized = false;
};

// Decoder plugin interface
class DecoderPlugin : public PluginBase
{
public:
    virtual bool canDecode(const QString &codecName) const = 0;
    virtual void* createDecoder(const QVariantMap &params) = 0;
    virtual void destroyDecoder(void *decoder) = 0;
};

// Demuxer plugin interface
class DemuxerPlugin : public PluginBase
{
public:
    virtual bool canOpen(const QString &url) const = 0;
    virtual void* openMedia(const QString &url, QVariantMap &info) = 0;
    virtual void closeMedia(void *handle) = 0;
    virtual bool readPacket(void *handle, QByteArray &data, QString &streamType) = 0;
};

// Network protocol plugin
class NetworkPlugin : public PluginBase
{
public:
    virtual bool supportsProtocol(const QString &protocol) const = 0;
    virtual void* connect(const QString &url, const QVariantMap &options) = 0;
    virtual void disconnect(void *handle) = 0;
    virtual int64_t read(void *handle, char *buffer, int64_t size) = 0;
    virtual int64_t seek(void *handle, int64_t offset, int whence) = 0;
};

// RAW decoder plugin (for proprietary RAW SDKs)
class RawDecoderPlugin : public PluginBase
{
public:
    virtual bool canDecode(const QString &extension) const = 0;
    virtual bool decode(const QString &filePath, QImage &output, QVariantMap &metadata) = 0;
    virtual QStringList supportedExtensions() const = 0;
};

// Cloud storage plugin interface
class CloudPlugin : public PluginBase
{
public:
    virtual bool canHandleUrl(const QString &url) const = 0;
    virtual void* openFile(const QString &url, const QString &mode) = 0;
    virtual void closeFile(void *handle) = 0;
    virtual int64_t readFile(void *handle, char *buffer, int64_t size) = 0;
    virtual bool listDirectory(const QString &path, QStringList &entries) = 0;
};

// AI plugin interface (future)
class AIPlugin : public PluginBase
{
public:
    virtual bool canProcess(const QString &taskType) const = 0;
    virtual QVariantMap process(const QVariantMap &input, const QVariantMap &params) = 0;
};

// Plugin factory function type
typedef PluginBase* (*CreatePluginFunc)();
typedef void (*DestroyPluginFunc)(PluginBase*);

// Plugin descriptor exported by shared libraries
struct PluginDescriptor {
    uint32_t apiVersion;
    CreatePluginFunc create;
    DestroyPluginFunc destroy;
};
