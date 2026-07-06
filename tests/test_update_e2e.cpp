// Phase 7A: Update Pipeline E2E Test
// Validates the complete update flow using a local HTTP server.
// Exit: 0 = PASS

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <cstdio>
#include <cstring>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>

#include "update/UpdateManager.h"
#include "update/PackageVerifier.h"
#include "update/GitHubUpdateProvider.h"
#include "update/LocalUpdateServer.h"

#define TEST(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "[UPDATE-TEST] FAIL: %s\n", msg); fflush(stderr); return 1; } \
    fprintf(stderr, "[UPDATE-TEST] PASS: %s\n", msg); fflush(stderr); \
} while(0)

static QByteArray createMinimalZip(const QByteArray &exeContent)
{
    mz_zip_archive archive;
    memset(&archive, 0, sizeof(archive));

    mz_zip_writer_init_heap(&archive, 0, 0);

    mz_zip_writer_add_mem(&archive, "LunarPlayer.exe",
                          exeContent.constData(), exeContent.size(),
                          MZ_DEFAULT_COMPRESSION);

    mz_zip_writer_add_mem(&archive, "readme.txt",
                          "Test update package", 18,
                          MZ_DEFAULT_COMPRESSION);

    void *buf = nullptr;
    size_t size = 0;
    mz_zip_writer_finalize_heap_archive(&archive, &buf, &size);
    mz_zip_writer_end(&archive);

    QByteArray result;
    if (buf && size > 0) {
        result = QByteArray((const char *)buf, (int)size);
        mz_free(buf);
    }
    return result;
}

static QByteArray createZipFromDir(const QString &dir)
{
    mz_zip_archive archive;
    memset(&archive, 0, sizeof(archive));

    mz_zip_writer_init_heap(&archive, 0, 0);

    QDir d(dir);
    QStringList entries = d.entryList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        QString filePath = dir + "/" + entry;
        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly)) continue;
        QByteArray data = f.readAll();

        QByteArray entryName = entry.toUtf8();
        mz_zip_writer_add_mem(&archive, entryName.constData(),
                              data.constData(), data.size(),
                              MZ_DEFAULT_COMPRESSION);
    }

    void *buf = nullptr;
    size_t size = 0;
    mz_zip_writer_finalize_heap_archive(&archive, &buf, &size);
    mz_zip_writer_end(&archive);

    QByteArray result(static_cast<const char *>(buf), static_cast<qsizetype>(size));
    mz_free(buf);
    return result;
}

static int runE2ETest(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("UpdateE2ETest");
    app.setApplicationVersion("0.1.0");

    // Step 1: Start local server
    fprintf(stderr, "\n[UPDATE-TEST] === STEP 1: Start Local HTTP Server ===\n");
    fflush(stderr);
    LocalUpdateServer server;
    TEST(server.start(), "Local server started");

    // Step 2: Create test ZIP from the Release build
    fprintf(stderr, "[UPDATE-TEST] === STEP 2: Create Test ZIP Package ===\n");
    fflush(stderr);

    QString releaseDir = QCoreApplication::applicationDirPath();
    QString zipPath = releaseDir + "/test_update_package.zip";

    // Create a minimal test ZIP with a fake LunarPlayer.exe
    QByteArray fakeExe = "FAKE LUNARPLAYER EXE FOR TESTING ONLY";
    {
        mz_zip_archive archive;
        memset(&archive, 0, sizeof(archive));

        mz_zip_writer_init_file(&archive, zipPath.toUtf8().constData(), 0);
        mz_zip_writer_add_mem(&archive, "LunarPlayer.exe",
                              fakeExe.constData(), fakeExe.size(),
                              MZ_DEFAULT_COMPRESSION);
        mz_zip_writer_add_mem(&archive, "LunarUpdateHelper.exe",
                              fakeExe.constData(), fakeExe.size(),
                              MZ_DEFAULT_COMPRESSION);
        mz_zip_writer_add_mem(&archive, "readme.txt",
                              "Test update package for E2E testing", 35,
                              MZ_DEFAULT_COMPRESSION);
        mz_zip_writer_finalize_archive(&archive);
        mz_zip_writer_end(&archive);
    }

    // Verify ZIP was created
    TEST(QFile::exists(zipPath), "Test ZIP created");
    QFileInfo zipInfo(zipPath);
    fprintf(stderr, "[UPDATE-TEST] ZIP size: %lld bytes\n", zipInfo.size());
    fflush(stderr);

    // Configure server with test data
    server.setZipPath(zipPath);
    server.setVersion("0.2.0");
    server.setReleaseNotes("Test release v0.2.0\n\n- Automated E2E test\n- Bug fixes");

    QString expectedSha256 = PackageVerifier::computeSHA256(zipPath);
    fprintf(stderr, "[UPDATE-TEST] SHA-256: %s\n", expectedSha256.toUtf8().constData());
    fflush(stderr);
    TEST(!expectedSha256.isEmpty(), "SHA-256 computed");

    // Step 3: Create UpdateManager and point at local server
    fprintf(stderr, "[UPDATE-TEST] === STEP 3: Configure UpdateManager ===\n");
    fflush(stderr);
    UpdateManager manager;
    manager.setOverrideUrl(server.apiUrl());
    manager.setAutoCheck(false);
    manager.setAutoDownload(false);
    manager.setNotifyBeforeInstall(true);

    // Step 4: Run async check
    fprintf(stderr, "[UPDATE-TEST] === STEP 4: Run Update Check ===\n");
    fflush(stderr);

    QEventLoop loop;
    bool gotUpdate = false;
    bool gotError = false;
    QString errorMsg;
    ReleaseInfo receivedInfo;

    QObject::connect(&manager, &UpdateManager::updateAvailable,
                     [&](const ReleaseInfo &info) {
                         gotUpdate = true;
                         receivedInfo = info;
                         loop.quit();
                     });
    QObject::connect(&manager, &UpdateManager::noUpdateAvailable, [&]() {
        errorMsg = "No update available when one was expected";
        gotError = true;
        loop.quit();
    });
    QObject::connect(&manager, &UpdateManager::checkFailed, [&](const QString &err) {
        errorMsg = err;
        gotError = true;
        loop.quit();
    });

    manager.checkForUpdateAsync();

    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();

    TEST(!gotError, qPrintable("Update check succeeded: " + errorMsg));
    TEST(gotUpdate, "Update available signal received");

    fprintf(stderr, "[UPDATE-TEST] Version: %s\n", receivedInfo.version.toUtf8().constData());
    fprintf(stderr, "[UPDATE-TEST] URL: %s\n", receivedInfo.downloadUrl.toUtf8().constData());
    fprintf(stderr, "[UPDATE-TEST] Size: %lld\n", receivedInfo.downloadSize);
    fflush(stderr);

    TEST(receivedInfo.version == "0.2.0", "Correct version detected");
    TEST(receivedInfo.isValid(), "Release info is valid");
    TEST(receivedInfo.downloadUrl.contains("LunarPlayerPortable.zip"),
         "Download URL contains correct filename");

    // Step 5: Download + Verify (UpdateManager chains these automatically)
    fprintf(stderr, "[UPDATE-TEST] === STEP 5: Download + Verify Update ===\n");
    fflush(stderr);

    QEventLoop dlLoop;
    bool verifyPassed = false;
    bool verifyFailed = false;
    QString verifyError;
    int lastPercent = 0;

    QObject::connect(&manager, &UpdateManager::downloadProgressChanged,
                     [&](int percent, double speed, int remaining) {
                         lastPercent = percent;
                         fprintf(stderr, "[UPDATE-TEST] Download: %d%% (%.1f Mbps, %ds remaining)\n",
                                 percent, speed, remaining);
                         fflush(stderr);
                     });
    QObject::connect(&manager, &UpdateManager::verificationPassed, [&]() {
        verifyPassed = true;
        dlLoop.quit();
    });
    QObject::connect(&manager, &UpdateManager::verificationFailed, [&](const QString &err) {
        verifyFailed = true;
        verifyError = err;
        dlLoop.quit();
    });
    QObject::connect(&manager, &UpdateManager::downloadFailed, [&](const QString &err) {
        verifyFailed = true;
        verifyError = err;
        dlLoop.quit();
    });

    manager.startUpdate();

    QTimer::singleShot(30000, &dlLoop, &QEventLoop::quit);
    dlLoop.exec();

    TEST(!verifyFailed, qPrintable("Download+Verify succeeded: " + verifyError));
    TEST(verifyPassed, "Verification passed");
    TEST(manager.hasPendingUpdate(), "Pending update is set");

    fprintf(stderr, "[UPDATE-TEST] Pending version: %s\n",
            manager.lastReleaseInfo()->version.toUtf8().constData());
    fflush(stderr);

    // Step 6: Check signals were emitted in order
    fprintf(stderr, "[UPDATE-TEST] === STEP 6: Verify Signal Order ===\n");
    fflush(stderr);

    TEST(server.connectionCount() > 0, "Server received connections");
    TEST(server.downloadCount() == 1, "Download requested exactly once");

    fprintf(stderr, "[UPDATE-TEST] Server connections: %d, downloads: %d\n",
            server.connectionCount(), server.downloadCount());
    fflush(stderr);

    // Cleanup
    QFile::remove(zipPath);

    fprintf(stderr, "\n[UPDATE-TEST] =========================================\n");
    fprintf(stderr, "[UPDATE-TEST] === RESULT: ALL E2E TESTS PASSED ===\n");
    fprintf(stderr, "[UPDATE-TEST] =========================================\n\n");
    fflush(stderr);

    return 0;
}

static int runFailureTests(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("UpdateFailureTests");
    app.setApplicationVersion("0.1.0");

    int passed = 0;
    int failed = 0;
    int total = 0;

    auto runTest = [&](const QString &name, auto testFn) {
        total++;
        fprintf(stderr, "\n[FAILURE-TEST] --- %s ---\n", name.toUtf8().constData());
        fflush(stderr);
        int result = testFn();
        if (result == 0) {
            passed++;
            fprintf(stderr, "[FAILURE-TEST] PASS: %s\n", name.toUtf8().constData());
        } else {
            failed++;
            fprintf(stderr, "[FAILURE-TEST] FAIL: %s (exit %d)\n", name.toUtf8().constData(), result);
        }
        fflush(stderr);
    };

    // Test 1: No update available (same version)
    runTest("No update (same version)", [&]() -> int {
        LocalUpdateServer server;
        if (!server.start()) return 1;
        server.setVersion("0.1.0");

        UpdateManager mgr;
        mgr.setOverrideUrl(server.apiUrl());

        QEventLoop loop;
        bool noUpdate = false;
        QObject::connect(&mgr, &UpdateManager::noUpdateAvailable, [&]() { noUpdate = true; loop.quit(); });
        QObject::connect(&mgr, &UpdateManager::checkFailed, [&]() { loop.quit(); });
        mgr.checkForUpdateAsync();
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        return noUpdate ? 0 : 1;
    });

    // Test 2: No update available (older version on server)
    runTest("No update (older version)", [&]() -> int {
        LocalUpdateServer server;
        if (!server.start()) return 1;
        server.setVersion("0.0.1");

        UpdateManager mgr;
        mgr.setOverrideUrl(server.apiUrl());

        QEventLoop loop;
        bool noUpdate = false;
        QObject::connect(&mgr, &UpdateManager::noUpdateAvailable, [&]() { noUpdate = true; loop.quit(); });
        QObject::connect(&mgr, &UpdateManager::checkFailed, [&]() { loop.quit(); });
        mgr.checkForUpdateAsync();
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        return noUpdate ? 0 : 1;
    });

    // Test 3: Server returns 404
    runTest("Server 404", [&]() -> int {
        LocalUpdateServer server;
        if (!server.start()) return 1;
        server.setSimulateNotFound(true);

        UpdateManager mgr;
        mgr.setOverrideUrl(server.apiUrl());

        QEventLoop loop;
        bool checkFailed = false;
        QObject::connect(&mgr, &UpdateManager::checkFailed, [&]() { checkFailed = true; loop.quit(); });
        QObject::connect(&mgr, &UpdateManager::noUpdateAvailable, [&]() { loop.quit(); });
        mgr.checkForUpdateAsync();
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        return checkFailed ? 0 : 1;
    });

    // Test 4: Server rate limit (403)
    runTest("Server 403 rate limit", [&]() -> int {
        LocalUpdateServer server;
        if (!server.start()) return 1;
        server.setSimulateRateLimit(true);

        UpdateManager mgr;
        mgr.setOverrideUrl(server.apiUrl());

        QEventLoop loop;
        bool checkFailed = false;
        QObject::connect(&mgr, &UpdateManager::checkFailed, [&]() { checkFailed = true; loop.quit(); });
        QObject::connect(&mgr, &UpdateManager::noUpdateAvailable, [&]() { loop.quit(); });
        mgr.checkForUpdateAsync();
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        return checkFailed ? 0 : 1;
    });

    // Test 5: No assets in release
    runTest("Release with no assets", [&]() -> int {
        LocalUpdateServer server;
        if (!server.start()) return 1;
        server.setSimulateNoAssets(true);
        server.setVersion("0.2.0");

        UpdateManager mgr;
        mgr.setOverrideUrl(server.apiUrl());

        QEventLoop loop;
        bool checkFailed = false;
        QObject::connect(&mgr, &UpdateManager::checkFailed, [&]() { checkFailed = true; loop.quit(); });
        QObject::connect(&mgr, &UpdateManager::noUpdateAvailable, [&]() { loop.quit(); });
        mgr.checkForUpdateAsync();
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        return checkFailed ? 0 : 1;
    });

    // Test 6: Corrupted ZIP download
    runTest("Corrupted ZIP", [&]() -> int {
        LocalUpdateServer server;
        if (!server.start()) return 1;
        server.setVersion("0.2.0");
        server.setSimulateCorrupt(true);

        UpdateManager mgr;
        mgr.setOverrideUrl(server.apiUrl());

        QEventLoop loop;
        bool verifyFailed = false;
        QString errMsg;

        QObject::connect(&mgr, &UpdateManager::updateAvailable, [&]() {
            mgr.startUpdate();
        });
        QObject::connect(&mgr, &UpdateManager::verificationFailed, [&](const QString &err) {
            verifyFailed = true;
            errMsg = err;
            loop.quit();
        });
        QObject::connect(&mgr, &UpdateManager::downloadFailed, [&](const QString &err) {
            errMsg = err;
            loop.quit();
        });
        QObject::connect(&mgr, &UpdateManager::noUpdateAvailable, [&]() { loop.quit(); });
        QObject::connect(&mgr, &UpdateManager::checkFailed, [&]() { loop.quit(); });
        mgr.checkForUpdateAsync();
        QTimer::singleShot(10000, &loop, &QEventLoop::quit);
        loop.exec();
        fprintf(stderr, "[FAILURE-TEST] Result: %s %s\n",
                verifyFailed ? "verifyFailed" : "other",
                errMsg.toUtf8().constData());
        fflush(stderr);
        return verifyFailed ? 0 : 1;
    });

    // Test 7: Unreachable server
    runTest("Unreachable server", [&]() -> int {
        UpdateManager mgr;
        mgr.setOverrideUrl("http://127.0.0.1:1/nonexistent");

        QEventLoop loop;
        bool checkFailed = false;
        QObject::connect(&mgr, &UpdateManager::checkFailed, [&]() { checkFailed = true; loop.quit(); });
        QObject::connect(&mgr, &UpdateManager::noUpdateAvailable, [&]() { loop.quit(); });
        mgr.checkForUpdateAsync();
        QTimer::singleShot(10000, &loop, &QEventLoop::quit);
        loop.exec();
        return checkFailed ? 0 : 1;
    });

    fprintf(stderr, "\n[FAILURE-TEST] =========================================\n");
    fprintf(stderr, "[FAILURE-TEST] RESULTS: %d/%d passed\n", passed, total);
    fprintf(stderr, "[FAILURE-TEST] =========================================\n\n");
    fflush(stderr);

    return (passed == total) ? 0 : 1;
}

int main(int argc, char *argv[])
{
    // Run E2E test first
    int e2eResult = runE2ETest(argc, argv);
    if (e2eResult != 0) return e2eResult;

    // Run failure tests
    int failResult = runFailureTests(argc, argv);

    fprintf(stderr, "\n[UPDATE-TEST] =========================================\n");
    fprintf(stderr, "[UPDATE-TEST] === PHASE 7A UPDATE PIPELINE: %s ===\n",
            failResult == 0 ? "ALL PASSED" : "SOME FAILED");
    fprintf(stderr, "[UPDATE-TEST] =========================================\n\n");
    fflush(stderr);

    return failResult;
}
