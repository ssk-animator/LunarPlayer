#include "PluginLoader.h"
#include <QDir>
#include <QFileInfo>

PluginLoader::PluginLoader(QObject *parent) : QObject(parent) {}

PluginLoader::~PluginLoader()
{
    unloadAll();
}

PluginBase* PluginLoader::loadPlugin(const QString &libraryPath)
{
    QFileInfo fi(libraryPath);
    if (!fi.exists()) {
        emit pluginError(libraryPath, "File not found");
        return nullptr;
    }

    auto *lib = new QLibrary(libraryPath, this);
    if (!lib->load()) {
        emit pluginError(libraryPath, lib->errorString());
        delete lib;
        return nullptr;
    }

    // Resolve plugin descriptor
    auto *desc = reinterpret_cast<PluginDescriptor*>(
        lib->resolve("lunarPluginDescriptor"));
    if (!desc) {
        // Try the legacy name
        auto getDesc = reinterpret_cast<PluginDescriptor*(*)()>(
            lib->resolve("getPluginDescriptor"));
        if (getDesc) desc = getDesc();
    }

    if (!desc) {
        emit pluginError(libraryPath, "No plugin descriptor found");
        lib->unload();
        delete lib;
        return nullptr;
    }

    if (desc->apiVersion != LUNAR_PLUGIN_API_VERSION) {
        emit pluginError(libraryPath, "Incompatible plugin API version");
        lib->unload();
        delete lib;
        return nullptr;
    }

    PluginBase *instance = desc->create();
    if (!instance) {
        emit pluginError(libraryPath, "Failed to create plugin instance");
        lib->unload();
        delete lib;
        return nullptr;
    }

    if (!instance->initialize()) {
        emit pluginError(libraryPath, "Plugin initialization failed");
        desc->destroy(instance);
        lib->unload();
        delete lib;
        return nullptr;
    }

    LoadedPlugin lp;
    lp.library = lib;
    lp.instance = instance;
    lp.destroy = desc->destroy;
    m_loadedPlugins.append(lp);

    emit pluginLoaded(instance->info().name, instance->info().types);
    return instance;
}

int PluginLoader::loadPluginsFromDir(const QString &directory)
{
    QDir dir(directory);
    int count = 0;

    QStringList filters;
#ifdef Q_OS_WIN
    filters << "*.dll";
#elif defined(Q_OS_MAC)
    filters << "*.dylib" << "*.so";
#else
    filters << "*.so";
#endif

    for (const QFileInfo &fi : dir.entryInfoList(filters, QDir::Files)) {
        if (loadPlugin(fi.absoluteFilePath()))
            ++count;
    }
    return count;
}

bool PluginLoader::unloadPlugin(PluginBase *plugin)
{
    for (int i = 0; i < m_loadedPlugins.size(); ++i) {
        if (m_loadedPlugins[i].instance == plugin) {
            LoadedPlugin &lp = m_loadedPlugins[i];
            QString name = lp.instance->info().name;
            lp.instance->shutdown();
            lp.destroy(lp.instance);
            lp.library->unload();
            delete lp.library;
            m_loadedPlugins.remove(i);
            emit pluginUnloaded(name);
            return true;
        }
    }
    return false;
}

void PluginLoader::unloadAll()
{
    for (auto &lp : m_loadedPlugins) {
        lp.instance->shutdown();
        lp.destroy(lp.instance);
        lp.library->unload();
        delete lp.library;
    }
    m_loadedPlugins.clear();
}

QVector<PluginBase*> PluginLoader::plugins() const
{
    QVector<PluginBase*> result;
    for (const auto &lp : m_loadedPlugins)
        result.append(lp.instance);
    return result;
}

QVector<PluginBase*> PluginLoader::pluginsByType(PluginType type) const
{
    QVector<PluginBase*> result;
    for (const auto &lp : m_loadedPlugins) {
        if (lp.instance->info().types & type)
            result.append(lp.instance);
    }
    return result;
}

PluginBase* PluginLoader::findPlugin(const QString &name) const
{
    for (const auto &lp : m_loadedPlugins) {
        if (lp.instance->info().name == name)
            return lp.instance;
    }
    return nullptr;
}
