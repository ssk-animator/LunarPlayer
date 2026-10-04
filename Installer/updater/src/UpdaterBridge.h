#pragma once

// UpdaterBridge — the updater-side narrow RPC contract. Same transport as
// the installer (WebViewHost::OnWebMessage -> Dispatch). Methods mirror
// the updater responsibilities in docs/UpdaterArchitecture.md and the
// docs/UpdateProtocol.md invocation contract. No binary replacement here:
//verified bundles are handed to LunarPlayerInstaller.exe.

#include "WebViewHost.h" // IRpcBackend

#include <cstdint>
#include <functional>
#include <string>

struct UpdateManifest {
    int schema = 0;
    std::wstring channel;
    std::wstring version;
    std::wstring file;
    std::wstring url;
    uint64_t size = 0;
    std::wstring sha256;
    std::wstring minimumUpdater;
};

class UpdaterBridge : public IRpcBackend {
public:
    UpdaterBridge(HINSTANCE hInstance, const std::wstring& updaterDir);
    ~UpdaterBridge() override = default;

    std::wstring Dispatch(const std::wstring& method,
                          const std::wstring& argsJson) override;

    // Offline self-test (unit checks, no network): returns exit code.
    static int SelfTest();
    // Live fixture flow (needs LUNAR_UPDATER_TEST_URL + a fixture dir with
    // install.json): GetUpdateState -> CheckForUpdate -> DownloadUpdate ->
    // VerifyUpdate, printing each verdict. Returns exit code.
    static int FixtureTest(const std::wstring& fixtureDir);

    // Progress sink for the download loop (posts to React).
    void SetProgressCallback(
        std::function<void(uint64_t, uint64_t)> cb) {
        m_progress = std::move(cb);
    }

private:
    std::wstring GetUpdateState(const std::wstring& args);
    std::wstring CheckForUpdate(const std::wstring& args);
    std::wstring DownloadUpdate(const std::wstring& args);
    std::wstring VerifyUpdate(const std::wstring& args);
    std::wstring WaitForPlayerExit(const std::wstring& args);
    std::wstring LaunchInstaller(const std::wstring& args);
    std::wstring RelaunchPlayer(const std::wstring& args);

    static bool ReadInstallJson(const std::wstring& dir,
                                std::wstring& version,
                                std::wstring& installPath,
                                std::wstring& channel);
    static bool FetchManifest(const std::wstring& url, UpdateManifest& out,
                              std::wstring& error);
    static int CompareVersions(const std::wstring& a,
                               const std::wstring& b); // -1/0/+1
    static bool DownloadFile(const std::wstring& url, const std::wstring& dest,
                             std::function<void(uint64_t, uint64_t)> progress,
                             std::wstring& error, bool& cancelled,
                             const volatile LONG* cancelFlag);
    static bool Sha256File(const std::wstring& path,
                           std::wstring& hexOut);
    static bool VerifySignature(const std::wstring& path,
                                std::wstring& publisherOut);
    static bool FindPlayerProcess(const std::wstring& exePath, DWORD& pid);

    HINSTANCE m_hInstance;
    std::wstring m_dir; // updater's own directory (staging lives here)
    std::wstring m_installedVersion;
    std::wstring m_installPath;
    std::wstring m_channel = L"stable";
    UpdateManifest m_manifest;
    bool m_manifestOk = false;
    std::wstring m_stagedPath;
    bool m_verified = false;
    volatile LONG m_cancelFlag = 0;
    std::function<void(uint64_t, uint64_t)> m_progress;
};
