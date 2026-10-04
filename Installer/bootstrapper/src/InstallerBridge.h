#pragma once

// InstallerBridge — the NARROW native <-> React contract. Exactly the
// methods in ui/src/bridge.ts, nothing more. React never touches the
// filesystem, processes, or shell; every privileged operation funnels
// through here with validation.
//
// Transport: React calls window.chrome.webview.postMessage({id, method,
// args}); the host replies {id, result} or {id, error}. Implemented in
// WebViewHost::OnWebMessage.

#include "RpcBackend.h" // IRpcBackend (narrow UI contract)

#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>

class LunarBA;

struct BridgeComponent {
    std::wstring id;          // core | ffmpeg | aiStudio | aiModels
    std::wstring name;
    std::wstring description;
    bool required = false;
    bool selected = false;
    uint64_t sizeBytes = 0;     // installed/on-disk size
    uint64_t downloadBytes = 0; // external pack attribution (0 = unknown)
};

// External download pack descriptor (Phase 3: GitHub release URLs by
// default; LUNAR_PACK_SOURCE local override for development). Size +
// SHA-256 verified before extraction.
struct DownloadPack {
    std::wstring file;   // e.g. LunarPlayer-Core.7z
    uint64_t size = 0;
    std::wstring sha256; // lowercase hex
    std::wstring url;    // full HTTPS URL (empty = unknown)
    bool present = false;
};

class InstallerBridge : public IRpcBackend {
public:
    explicit InstallerBridge(LunarBA* ba);
    ~InstallerBridge() override = default;

    std::wstring Dispatch(const std::wstring& method,
                          const std::wstring& argsJson) override;

    // State for install.json (written by LunarBA on successful Apply).
    std::wstring InstallPath() const { return m_installPath; }
    void SetInstallPath(const std::wstring& p) { m_installPath = p; }
    std::vector<std::wstring> SelectedIds() const { return m_selected; }

    // NativeUI direct API (no JSON): same data and validation as the
    // RPC methods, for the Direct2D installer (no WebView2).
    std::vector<BridgeComponent> Components();
    DownloadPack CorePack();
    DownloadPack AiPack();
    uint64_t SelectedBytes();
    uint64_t AvailableBytes();
    std::wstring BundleVersion();
    void SetOptionalSelected(const std::wstring& id, bool selected);
    bool IsSelected(const std::wstring& id) const;
    // Folder picker with a proper owner window; updates m_installPath.
    bool BrowseForFolder(HWND owner);
    bool StartInstallNative();
    void CancelInstallNative();
    // Launches the installed player; returns false + message on failure.
    bool LaunchPlayerNative(std::wstring& error);
    // Launch-after-install option (real: runs the installed player on
    // successful Apply) + packaged logo path for the native title bar.
    bool IsLaunchAfter() const { return m_launchAfter; }
    void SetLaunchAfter(bool on) { m_launchAfter = on; }
    std::wstring LogoPath() const;

private:
    std::wstring GetInstallationInfo(const std::wstring& args);
    std::wstring BrowseForFolder(const std::wstring& args);
    std::wstring GetDiskSpace(const std::wstring& args);
    std::wstring SelectComponents(const std::wstring& args);
    std::wstring StartInstall(const std::wstring& args);
    std::wstring CancelInstall(const std::wstring& args);
    std::wstring LaunchPlayer(const std::wstring& args);

    // Component sizes computed from the REAL staged payload
    // (Installer/staging layout produced by package-release.*). Until a
    // package is staged, sizes reflect the local development build
    // outputs — still measured, never hardcoded.
    std::vector<BridgeComponent> ProbeComponents();
    static uint64_t DirSizeBytes(const std::wstring& dir,
                                 const wchar_t* const* names, size_t count);

    LunarBA* m_ba; // not owned
    std::vector<std::wstring> m_selected;
    std::wstring m_installPath;
    // Opt-in only: the installer NEVER auto-starts the player. When the
    // user checks the Advanced option, completion launches once.
    bool m_launchAfter = false;
    DownloadPack m_packs[2];
};
