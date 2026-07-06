#include <cstdio>
#include <QDir>
#include <QCoreApplication>
#include "plugin/PluginLoader.h"

#define TEST(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s\n", msg); return 1; \
    } \
    printf("PASS: %s\n", msg); \
} while(0)

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    printf("=== Phase 13: Plugin Loading Test ===\n\n");

    QString binDir = QCoreApplication::applicationDirPath();
    QString pluginsDir = binDir + "/plugins";
    printf("Plugin directory: %s\n", qPrintable(pluginsDir));

    PluginLoader loader;

    // Test 1: Load plugins from directory
    printf("\n--- Test 1: Load sample plugins ---\n");
    int count = loader.loadPluginsFromDir(pluginsDir);
    printf("  Plugins loaded: %d\n", count);
    TEST(count >= 1, "At least one plugin loaded");

    // Test 2: Query loaded plugins
    printf("\n--- Test 2: Query plugins ---\n");
    auto allPlugins = loader.plugins();
    TEST(allPlugins.size() >= 1, "At least 1 plugin in list");
    printf("  Found %d plugins:\n", allPlugins.size());
    for (auto *p : allPlugins) {
        PluginInfo pi = p->info();
        printf("    - %s v%s by %s\n",
               qPrintable(pi.name), qPrintable(pi.version), qPrintable(pi.author));
        printf("      Description: %s\n", qPrintable(pi.description));
    }

    // Test 3: Find by type
    printf("\n--- Test 3: Find by type ---\n");
    auto decoders = loader.pluginsByType(PluginType::Decoder);
    printf("  Decoder plugins: %d\n", decoders.size());
    if (decoders.size() > 0) {
        PluginInfo di = decoders[0]->info();
        TEST(di.types & PluginType::Decoder, "Plugin has Decoder type flag");
    }

    auto networks = loader.pluginsByType(PluginType::Network);
    printf("  Network plugins: %d\n", networks.size());
    if (networks.size() > 0) {
        PluginInfo ni = networks[0]->info();
        TEST(ni.types & PluginType::Network, "Plugin has Network type flag");
    }

    // Test 4: Find by name
    printf("\n--- Test 4: Find by name ---\n");
    PluginBase *p = loader.findPlugin("Sample Decoder Plugin");
    if (p) {
        TEST(p->isInitialized(), "Sample Decoder Plugin is initialized");
        printf("  Found: %s\n", qPrintable(p->info().name));

        auto *dp = dynamic_cast<DecoderPlugin*>(p);
        TEST(dp != nullptr, "Can cast to DecoderPlugin");
        if (dp) {
            TEST(dp->canDecode("test_codec"), "Can decode test_codec");
            TEST(!dp->canDecode("h264"), "Cannot decode h264");
            void *dec = dp->createDecoder(QVariantMap());
            TEST(dec != nullptr, "Created decoder handle");
            dp->destroyDecoder(dec);
        }
    } else {
        p = loader.findPlugin("Sample Network Plugin");
        if (p) {
            TEST(p->isInitialized(), "Sample Network Plugin is initialized");
            auto *np = dynamic_cast<NetworkPlugin*>(p);
            TEST(np != nullptr, "Can cast to NetworkPlugin");
            if (np) {
                TEST(np->supportsProtocol("testnet"), "Supports testnet protocol");
                TEST(!np->supportsProtocol("http"), "Does not support http");
            }
        }
    }

    // Test 5: Unload specific plugin
    printf("\n--- Test 5: Unload ---\n");
    if (allPlugins.size() > 0) {
        QString name = allPlugins[0]->info().name;
        bool unloaded = loader.unloadPlugin(allPlugins[0]);
        TEST(unloaded, "Plugin unloaded successfully");
        printf("  Unloaded: %s\n", qPrintable(name));
    }

    // Test 6: Unload all remaining
    printf("\n--- Test 6: Unload all ---\n");
    loader.unloadAll();
    TEST(loader.plugins().isEmpty(), "No plugins after unload all");

    printf("\n*** ALL PLUGIN LOADING TESTS PASSED ***\n");
    return 0;
}
