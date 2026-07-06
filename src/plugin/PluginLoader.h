#pragma once
#include <QObject>
#include <QVector>
#include <QMap>
#include <QString>
#include <QLibrary>
#include "PluginAPI.h"

class PluginLoader : public QObject
{
    Q_OBJECT
public:
    explicit PluginLoader(QObject *parent = nullptr);
    ~PluginLoader();

    // Load a plugin from a shared library (.dll / .so / .dylib)
    PluginBase* loadPlugin(const QString &libraryPath);

    // Load all plugins from a directory
    int loadPluginsFromDir(const QString &directory);

    // Unload a specific plugin
    bool unloadPlugin(PluginBase *plugin);

    // Unload all plugins
    void unloadAll();

    // Query loaded plugins
    QVector<PluginBase*> plugins() const;
    QVector<PluginBase*> pluginsByType(PluginType type) const;

    // Find a specific plugin by name
    PluginBase* findPlugin(const QString &name) const;

signals:
    void pluginLoaded(const QString &name, PluginType type);
    void pluginUnloaded(const QString &name);
    void pluginError(const QString &path, const QString &error);

private:
    struct LoadedPlugin {
        QLibrary *library = nullptr;
        PluginBase *instance = nullptr;
        DestroyPluginFunc destroy = nullptr;
    };

    QVector<LoadedPlugin> m_loadedPlugins;
};
