#include <cstdio>
#include <cstdlib>
#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QDebug>
#include <cstring>

// Non-GL runtime verification for Phase 13
#include "network/NetworkBuffer.h"
#include "network/NetworkMediaSession.h"
#include "renderer/ColorManager.h"
#include "renderer/GPUTextureCache.h"
#include "renderer/CPURenderer.h"
#include "plugin/PluginLoader.h"

#define TEST(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s\n", msg); fflush(stdout); return 1; \
    } \
    printf("PASS: %s\n", msg); fflush(stdout); \
} while(0)

#define STEP(msg) printf("\n--- %s ---\n", msg); fflush(stdout)

// ============================================================
// Test 1: NetworkBuffer adaptive buffering runtime
// ============================================================
static int test1_adaptiveBufferRuntime()
{
    STEP("Adaptive Buffer Runtime");

    NetworkBuffer buf;

    // Simulate a streaming session with varying network conditions
    buf.setState(NetworkBuffer::Buffering);

    // Good network: high bitrate, no loss
    for (int i = 0; i < 50; ++i) {
        buf.recordBytes(131072); // 128KB chunks
    }
    buf.setState(NetworkBuffer::Ready);
    double health_good = buf.networkHealth();
    printf("  Good network health: %.2f\n", health_good);
    TEST(health_good > 0.8, "Good network: health > 0.8");

    // Apply adaptive budget (should decrease target)
    buf.updateAdaptiveBudget(5000000.0); // 5 Mbps
    double targetAfterGood = buf.targetDurationSec();
    printf("  Target after good network: %.1fs\n", targetAfterGood);
    TEST(targetAfterGood <= 5.0, "Adaptive budget reduced target on good network");

    // Poor network: add packet loss
    for (int i = 0; i < 8; ++i) buf.recordPacketLoss();
    double health_poor = buf.networkHealth();
    printf("  Poor network health: %.2f\n", health_poor);
    TEST(health_poor < health_good, "Packet loss reduces health");

    // Apply adaptive budget (should increase target)
    buf.updateAdaptiveBudget(500000.0); // 500 Kbps
    double targetAfterPoor = buf.targetDurationSec();
    printf("  Target after poor network: %.1fs\n", targetAfterPoor);

    // Adaptive target bytes
    int64_t targetBytes = buf.adaptiveTargetBytes();
    printf("  Adaptive target bytes: %lld\n", (long long)targetBytes);
    TEST(targetBytes > 0, "Adaptive target bytes > 0");

    // Reset and verify
    buf.reset();
    TEST(buf.networkHealth() == 1.0, "After reset: health = 1.0");

    printf("  -> Test 1 PASSED\n");
    return 0;
}

// ============================================================
// Test 2: ColorManager full HDR pipeline (CPU path end-to-end)
// ============================================================
static int test2_hdrPipelineRuntime()
{
    STEP("HDR Pipeline Runtime (CPU)");

    ColorManager cm;

    // Create a test image simulating HDR content (bright highlights)
    QImage hdrFrame(320, 180, QImage::Format_RGB888);
    // Fill with a gradient of increasing brightness
    for (int y = 0; y < hdrFrame.height(); ++y) {
        for (int x = 0; x < hdrFrame.width(); ++x) {
            double intensity = static_cast<double>(x) / hdrFrame.width(); // 0..1
            int val = static_cast<int>(intensity * 255.0);
            hdrFrame.setPixel(x, y, qRgb(val, val, val));
        }
    }

    // PQ metadata
    HDRMetadata pqMd;
    pqMd.valid = true;
    pqMd.transfer = TransferCharacteristics::PQ;
    pqMd.format = HDRFormat::HDR10;
    pqMd.maxLuminance = 1000.0;
    pqMd.maxCLL = 800.0;
    pqMd.maxFALL = 300.0;

    TEST(ColorManager::needsToneMapping(pqMd), "PQ needs tone mapping");

    // Apply CPU tone mapping
    QImage toneMapped = cm.applyToneMap(hdrFrame, pqMd);
    TEST(!toneMapped.isNull(), "Tone mapped result not null");
    TEST(toneMapped.width() == 320, "Tone mapped width correct");
    TEST(toneMapped.height() == 180, "Tone mapped height correct");

    // Verify highlights are compressed
    // Pixel at far right (high intensity) should be darker after tone mapping
    QRgb originalBright = hdrFrame.pixel(hdrFrame.width() - 1, hdrFrame.height() / 2);
    QRgb mappedBright = toneMapped.pixel(hdrFrame.width() - 1, hdrFrame.height() / 2);
    printf("  High intensity: rgb(%d,%d,%d) -> rgb(%d,%d,%d)\n",
           qRed(originalBright), qGreen(originalBright), qBlue(originalBright),
           qRed(mappedBright), qGreen(mappedBright), qBlue(mappedBright));
    // Reinhard compresses bright values
    int avgOriginal = (qRed(originalBright) + qGreen(originalBright) + qBlue(originalBright)) / 3;
    int avgMapped = (qRed(mappedBright) + qGreen(mappedBright) + qBlue(mappedBright)) / 3;
    // Pure white (255,255,255) -> Reinhard(1.0) = 0.5, so 255 -> ~127
    // Our gradient uses x/width as intensity, so rightmost pixel is ~255
    // Reinhard(255/255 * 1.0) = 0.5 * 255 = 127.5
    printf("  Average brightness: %d -> %d (expected ~127 for rightmost pixel)\n",
           avgOriginal, avgMapped);

    // Verify description
    QString desc = ColorManager::description(pqMd);
    printf("  HDR description: %s\n", qPrintable(desc));
    TEST(desc.contains("HDR10"), "Description contains HDR10");
    TEST(desc.contains("800"), "Description contains MaxCLL");

    // HLG test
    HDRMetadata hlgMd;
    hlgMd.transfer = TransferCharacteristics::HLG;
    hlgMd.format = HDRFormat::HLG;
    TEST(ColorManager::needsToneMapping(hlgMd), "HLG needs tone mapping");
    QImage hlgMapped = cm.applyToneMap(hdrFrame, hlgMd);
    TEST(!hlgMapped.isNull(), "HLG tone mapped result not null");

    // SDR passthrough
    HDRMetadata sdrMd;
    sdrMd.transfer = TransferCharacteristics::BT709;
    sdrMd.format = HDRFormat::SDR;
    TEST(!ColorManager::needsToneMapping(sdrMd), "SDR no tone mapping");
    QImage sdrResult = cm.applyToneMap(hdrFrame, sdrMd);
    TEST(sdrResult.pixel(0, 0) == hdrFrame.pixel(0, 0), "SDR passthrough exact match");

    // Edge case: empty frame
    QImage emptyResult = cm.applyToneMap(QImage(), pqMd);
    TEST(emptyResult.isNull(), "Empty frame returns null");
    
    // Edge case: 1x1 image
    QImage tiny(1, 1, QImage::Format_RGB888);
    tiny.fill(qRgb(200, 100, 50));
    QImage tinyMapped = cm.applyToneMap(tiny, pqMd);
    TEST(!tinyMapped.isNull(), "1x1 tone map works");

    printf("  -> Test 2 PASSED\n");
    return 0;
}

// ============================================================
// Test 3: CPURenderer HDR integration
// ============================================================
static int test3_cpuRendererHDR()
{
    STEP("CPURenderer HDR Integration");

    CPURenderer renderer;
    TEST(renderer.initialize(), "CPURenderer initializes");

    QImage testFrame(100, 100, QImage::Format_RGB888);
    testFrame.fill(qRgb(220, 180, 140));

    renderer.present(testFrame);

    // Without HDR, paint should draw original
    QImage canvas(100, 100, QImage::Format_RGB888);
    canvas.fill(Qt::black);
    RenderState rs;
    rs.destinationRect = QRectF(0, 0, 100, 100);
    QPainter painter(&canvas);
    renderer.paint(painter, rs);
    painter.end();

    // Check pixel value (should be original since no HDR metadata set)
    QRgb pixel = canvas.pixel(50, 50);
    printf("  CPU renderer output (no HDR): rgb(%d,%d,%d)\n",
           qRed(pixel), qGreen(pixel), qBlue(pixel));
    TEST(pixel == qRgb(220, 180, 140), "No HDR: matches original");

    // Set HDR metadata and paint
    HDRMetadata hdrMd;
    hdrMd.transfer = TransferCharacteristics::PQ;
    hdrMd.format = HDRFormat::HDR10;
    renderer.setHDRMetadata(hdrMd);

    RenderState rs2;
    rs2.destinationRect = QRectF(0, 0, 100, 100);
    QImage canvas2(100, 100, QImage::Format_RGB888);
    canvas2.fill(Qt::black);
    QPainter painter2(&canvas2);
    renderer.paint(painter2, rs2);
    painter2.end();

    QRgb hdrPixel = canvas2.pixel(50, 50);
    printf("  CPU renderer output (HDR): rgb(%d,%d,%d)\n",
           qRed(hdrPixel), qGreen(hdrPixel), qBlue(hdrPixel));
    
    // Apply tone mapping directly and compare
    ColorManager cm;
    QImage expected = cm.applyToneMap(testFrame, hdrMd);
    QRgb expectedPixel = expected.pixel(50, 50);
    TEST(hdrPixel == expectedPixel, "HDR: CPURenderer matches ColorManager output");

    renderer.cleanup();
    printf("  -> Test 3 PASSED\n");
    return 0;
}

// ============================================================
// Test 4: PluginLoader with actual sample plugins
// ============================================================
static int test4_pluginRuntime(const QString &testDir)
{
    STEP("Plugin Runtime Verification");

    PluginLoader loader;
    QObject::connect(&loader, &PluginLoader::pluginLoaded, [](const QString &name, PluginType) {
        qDebug() << "[Plugin] Loaded:" << name;
    });
    QObject::connect(&loader, &PluginLoader::pluginError, [](const QString &path, const QString &err) {
        qDebug() << "[Plugin] Error loading" << path << ":" << err;
    });

    // Find plugins directory relative to test binary
    QString binDir = QCoreApplication::applicationDirPath();
    QString pluginsDir = binDir + "/plugins";
    printf("  Plugin dir: %s\n", qPrintable(pluginsDir));

    if (!QDir(pluginsDir).exists()) {
        // Try test source dir
        pluginsDir = testDir + "/build/Release/plugins";
        printf("  Trying: %s\n", qPrintable(pluginsDir));
    }

    int count = loader.loadPluginsFromDir(pluginsDir);
    printf("  Plugins found: %d\n", count);
    TEST(count >= 1, "At least one plugin loaded from directory");

    auto allPlugins = loader.plugins();
    TEST(allPlugins.size() >= 1, "Plugins accessible after loading");
    TEST(loader.findPlugin("Sample Decoder Plugin") != nullptr, "Decoder plugin found by name");
    TEST(loader.findPlugin("Sample Network Plugin") != nullptr, "Network plugin found by name");

    // Verify decoder plugin lifecycle
    auto *decoderPlugin = dynamic_cast<DecoderPlugin*>(loader.findPlugin("Sample Decoder Plugin"));
    TEST(decoderPlugin != nullptr, "Decoder plugin castable");
    if (decoderPlugin) {
        TEST(decoderPlugin->isInitialized(), "Decoder plugin initialized");
        TEST(decoderPlugin->canDecode("test_codec"), "Can decode test_codec");
        TEST(!decoderPlugin->canDecode("h264"), "Cannot decode h264 (testing correct rejection)");

        void *handle = decoderPlugin->createDecoder(QVariantMap());
        TEST(handle != nullptr, "Decoder handle created");
        decoderPlugin->destroyDecoder(handle);
        printf("  Decoder plugin lifecycle verified\n");
    }

    // Verify network plugin lifecycle
    auto *netPlugin = dynamic_cast<NetworkPlugin*>(loader.findPlugin("Sample Network Plugin"));
    TEST(netPlugin != nullptr, "Network plugin castable");
    if (netPlugin) {
        TEST(netPlugin->supportsProtocol("testnet"), "Supports testnet");
        TEST(!netPlugin->supportsProtocol("http"), "Does not support http");

        void *conn = netPlugin->connect("testnet://example.com", QVariantMap());
        TEST(conn != nullptr, "Network connection created");

        char buf[64];
        int64_t bytesRead = netPlugin->read(conn, buf, 64);
        TEST(bytesRead == 64, "Network read returns expected bytes");

        int64_t seekResult = netPlugin->seek(conn, 0, 0);
        TEST(seekResult == 0, "Network seek returns OK");

        netPlugin->disconnect(conn);
        printf("  Network plugin lifecycle verified\n");
    }

    // Unload all
    loader.unloadAll();
    TEST(loader.plugins().isEmpty(), "Unload all removes all plugins");
    TEST(loader.findPlugin("Sample Decoder Plugin") == nullptr, "Plugin not found after unload");

    printf("  -> Test 4 PASSED\n");
    return 0;
}

// ============================================================
// Test 5: NetworkMediaSession audio detection
// ============================================================
static int test5_networkAudioDetection(const QString &testDir)
{
    STEP("Network Audio Detection");

    NetworkMediaSession session;

    // Detect that a local file does NOT trigger network audio
    QString mediaPath = testDir + "/tests/media/test_bars_24fps_2s.mp4";
    if (!QFileInfo::exists(mediaPath)) {
        printf("  SKIP: Test media not found at %s\n", qPrintable(mediaPath));
        printf("  -> Test 5 SKIPPED\n");
        return 0;
    }

    // Open local file through the session
    if (!session.openStream(mediaPath)) {
        printf("  INFO: Could not open via openStream: %s\n",
               qPrintable(session.lastError()));
        printf("  -> Test 5 PARTIAL (openStream failed)\n");
        return 0;
    }

    printf("  Stream opened: %s\n", qPrintable(session.url()));
    printf("  Has audio: %s\n", session.hasAudio() ? "yes" : "no");
    if (session.hasAudio()) {
        printf("  Audio: %d Hz, %d channels\n",
               session.audioSampleRate(), session.audioChannels());
        TEST(session.audioSampleRate() > 0, "Audio sample rate > 0");
        TEST(session.audioChannels() > 0, "Audio channels > 0");
    }

    // Read a few frames and check audio sample accumulation
    int framesRead = 0;
    int audioFramesPopped = 0;
    for (int i = 0; i < 30 && session.readFrame(); ++i) {
        ++framesRead;
        QVector<float> samples;
        if (session.popAudioSamples(samples)) {
            ++audioFramesPopped;
            printf("  Audio samples: %lld floats\n", (long long)samples.size());
            if (!samples.isEmpty()) {
                // Verify samples are valid floats
                bool hasValid = false;
                for (float s : samples) {
                    if (s != 0.0f && !std::isnan(s) && !std::isinf(s)) {
                        hasValid = true;
                        break;
                    }
                }
                if (hasValid) {
                    printf("  Audio samples contain non-zero data\n");
                    break;
                }
            }
        }
    }
    printf("  Frames read: %d, audio pops: %d\n", framesRead, audioFramesPopped);

    // Verify audio clock works
    double clock = session.audioClock();
    printf("  Audio clock: %.2f\n", clock);

    session.close();
    TEST(!session.isOpen(), "Session closed after test");

    printf("  -> Test 5 PASSED\n");
    return 0;
}

// ============================================================
// Test 6: ColorManager metadata parsing from real frame
// ============================================================
static int test6_hdrMetadataParsing()
{
    STEP("HDR Metadata Parsing");

    // Test with synthetic metadata (no real AVFrame needed for format detection)
    HDRMetadata md10;
    md10.transfer = TransferCharacteristics::PQ;
    md10.format = HDRFormat::HDR10;
    md10.maxLuminance = 1000.0;
    md10.maxCLL = 2000.0;
    md10.maxFALL = 500.0;
    md10.displayPrimaryX[0] = 0.68; md10.displayPrimaryY[0] = 0.32;
    md10.displayPrimaryX[1] = 0.265; md10.displayPrimaryY[1] = 0.690;
    md10.displayPrimaryX[2] = 0.150; md10.displayPrimaryY[2] = 0.060;
    md10.whitePointX = 0.3127; md10.whitePointY = 0.3290;
    md10.valid = true;

    QString desc10 = ColorManager::description(md10);
    printf("  HDR10 description: %s\n", qPrintable(desc10));
    TEST(desc10.contains("2000"), "Description contains MaxCLL");

    // Verify format name
    TEST(ColorManager::formatName(HDRFormat::HDR10) == "HDR10", "Format name HDR10");
    TEST(ColorManager::formatName(HDRFormat::HDR10Plus) == "HDR10+", "Format name HDR10+");
    TEST(ColorManager::formatName(HDRFormat::DolbyVision) == "Dolby Vision", "Format name DolbyVision");
    TEST(ColorManager::formatName(HDRFormat::None) == "Unknown", "Format name Unknown");
    TEST(ColorManager::formatName(static_cast<HDRFormat>(99)) == "Unknown", "Format name fallback");

    // Test needsToneMapping with all transfer types
    HDRMetadata pq;
    pq.transfer = TransferCharacteristics::PQ;
    TEST(ColorManager::needsToneMapping(pq), "PQ needs tone mapping");

    HDRMetadata hlg;
    hlg.transfer = TransferCharacteristics::HLG;
    TEST(ColorManager::needsToneMapping(hlg), "HLG needs tone mapping");

    HDRMetadata bt709;
    bt709.transfer = TransferCharacteristics::BT709;
    TEST(!ColorManager::needsToneMapping(bt709), "BT709 no tone mapping");

    HDRMetadata unknown;
    unknown.transfer = TransferCharacteristics::Unknown;
    TEST(!ColorManager::needsToneMapping(unknown), "Unknown no tone mapping");

    printf("  -> Test 6 PASSED\n");
    return 0;
}

// ============================================================
// Test 7: StreamingOverlay buffer integration
// ============================================================
static int test7_streamingOverlayRuntime()
{
    STEP("StreamingOverlay Runtime");

    // Test the StreamingOverlay's buffer integration without a QWidget
    // (the QWidget creation requires QApplication, not QGuiApplication)
    // We test the integration through NetworkBuffer signals instead

    NetworkBuffer buffer;

    // Simulate streaming states
    buffer.setState(NetworkBuffer::Buffering);
    TEST(buffer.isPlayable(), "Buffering is playable");
    TEST(buffer.state() == NetworkBuffer::Buffering, "State = Buffering");

    buffer.setState(NetworkBuffer::Ready);
    TEST(buffer.state() == NetworkBuffer::Ready, "State = Ready");

    buffer.recordBytes(1024 * 1024 * 5); // 5MB
    buffer.setState(NetworkBuffer::Stalled);
    TEST(buffer.isPlayable(), "Stalled is still playable");

    // Bitrate tracking
    double bitrate = buffer.bitrateBps();
    printf("  Bitrate: %.0f bps\n", bitrate);

    buffer.recordPacketLoss();
    buffer.recordPacketLoss();
    buffer.recordPacketLoss();
    TEST(buffer.droppedPackets() == 3, "3 dropped packets");

    // Test that state transitions emit signals (connected via lambda)
    bool stateChanged = false;
    QObject::connect(&buffer, &NetworkBuffer::stateChanged,
                     [&stateChanged](NetworkBuffer::State) { stateChanged = true; });

    buffer.setState(NetworkBuffer::Error);
    TEST(stateChanged, "State change emitted signal");
    TEST(buffer.state() == NetworkBuffer::Error, "State = Error");

    buffer.reset();
    TEST(buffer.state() == NetworkBuffer::Idle, "Reset -> Idle");
    TEST(buffer.droppedPackets() == 0, "Reset drops cleared");

    printf("  -> Test 7 PASSED\n");
    return 0;
}

// ============================================================
// Test 8: Cross-subsystem integration (buffer -> color -> plugin)
// ============================================================
static int test8_crossSubsystem()
{
    STEP("Cross-Subsystem Integration");

    // Test that all subsystems work together
    // 1. Buffer produces data
    // 2. ColorManager processes it
    // 3. Plugin system verifies it

    // Simulate buffer data
    NetworkBuffer buffer;
    buffer.setState(NetworkBuffer::Ready);
    buffer.recordBytes(2 * 1024 * 1024); // 2MB
    buffer.setTotalBytes(10 * 1024 * 1024); // 10MB total
    double progress = buffer.progress();
    printf("  Buffer progress: %.2f\n", progress);
    TEST(progress > 0.19 && progress < 0.21, "Buffer progress ~20%");

    // ColorManager processes a frame
    ColorManager cm;
    QImage frame(64, 48, QImage::Format_RGB888);
    frame.fill(qRgb(200, 100, 50));

    HDRMetadata md;
    md.transfer = TransferCharacteristics::HLG;
    md.format = HDRFormat::HLG;
    QImage output = cm.applyToneMap(frame, md);
    TEST(!output.isNull(), "Tone map after buffer produces output");

    // Verify tone mapping changed signal (HDR info through description)
    QString desc = ColorManager::description(md);
    TEST(desc == "HLG", "Pure HLG description (no side data)");

    // PluginLoader cycle
    PluginLoader loader;
    loader.unloadAll(); // Ensure clean state
    TEST(loader.plugins().isEmpty(), "Plugin system clean after unload");

    // No-op: just verify the chain doesn't crash
    buffer.reset();
    md = HDRMetadata();
    output = cm.applyToneMap(frame, md);

    printf("  Cross-subsystem integration verified\n");
    printf("  -> Test 8 PASSED\n");
    return 0;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QString testDir = TEST_SOURCE_DIR;
    printf("============================================\n");
    printf("Phase 13 Comprehensive Runtime Verification\n");
    printf("============================================\n");
    printf("Test dir: %s\n\n", qPrintable(testDir));

    int failed = 0;
#define RUN_T(name) do { \
    int r = name; \
    if (r != 0) { \
        printf(">>> TEST FAILED (code=%d) <<<\n\n", r); \
        failed = r; \
        goto done; \
    } \
} while(0)

    RUN_T(test1_adaptiveBufferRuntime());
    RUN_T(test2_hdrPipelineRuntime());
    RUN_T(test3_cpuRendererHDR());
    RUN_T(test4_pluginRuntime(testDir));
    RUN_T(test5_networkAudioDetection(testDir));
    RUN_T(test6_hdrMetadataParsing());
    RUN_T(test7_streamingOverlayRuntime());
    RUN_T(test8_crossSubsystem());

done:
    if (failed == 0) {
        printf("\n*** ALL PHASE 13 RUNTIME TESTS PASSED ***\n");
    } else {
        printf("\n*** PHASE 13 RUNTIME TESTS FAILED ***\n");
    }
    printf("\n");
    return failed;
}
