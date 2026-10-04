#pragma once

// LunarBA — Lunar Player custom Bootstrapper Application (native C++,
// no .NET). Derives the generated LunarBABase (do-nothing defaults) and
// overrides only the lifecycle callbacks that drive installation:
//
//   OnStartup          -> create UI thread (STA) + native installer UI
//   OnDetectComplete   -> report detection to UI (installed version?)
//   OnPlanComplete     -> engine->Apply() (unless user cancelled)
//   OnExecuteProgress/OnProgress -> forward percent/phase to React
//   OnError            -> forward to React, honor cancel
//   OnApplyComplete    -> write install.json, report result to React
//   OnShutdown         -> teardown
//
// Threading: Burn invokes BA methods on engine threads. All native-UI
// and bridge work happens on a dedicated STA UI thread; engine callbacks
// bridge work happens on a dedicated STA UI thread; engine callbacks are
// marshaled there via a hidden message window. Cancel is cooperative:
// the flag is set from the UI thread and observed in OnProgress (IDCANCEL).

#include "LunarBABase.h"

#include <string>
#include <windows.h>

class NativeInstallerWindow;
class InstallerBridge;

enum class LunarInstallState {
    Idle,
    Detecting,
    Ready,
    Planning,
    Applying,
    Cancelling,
    Complete,
    Failed,
};

class LunarBA : public LunarBABase {
public:
    LunarBA(HINSTANCE hInstance, IBootstrapperEngine* pEngine,
            const BOOTSTRAPPER_COMMAND* pCommand);
    virtual ~LunarBA();

    // IBootstrapperApplication lifecycle overrides (exact v3 signatures).
    STDMETHODIMP OnStartup() override;
    STDMETHODIMP_(int) OnShutdown() override;
    STDMETHODIMP_(void) OnDetectComplete(
        __in HRESULT hrStatus) override;
    STDMETHODIMP_(void) OnPlanComplete(
        __in HRESULT hrStatus) override;
    STDMETHODIMP_(int) OnCacheAcquireProgress(
        __in_z_opt LPCWSTR wzPackageOrContainerId,
        __in_z_opt LPCWSTR wzPayloadId,
        __in DWORD64 dw64Progress,
        __in DWORD64 dw64Total,
        __in DWORD dwOverallPercentage) override;
    STDMETHODIMP_(int) OnResolveSource(
        __in_z LPCWSTR wzPackageOrContainerId,
        __in_z_opt LPCWSTR wzPayloadId,
        __in_z LPCWSTR wzLocalSource,
        __in_z_opt LPCWSTR wzDownloadSource) override;
    STDMETHODIMP_(void) OnCacheComplete(
        __in HRESULT hrStatus) override;
    STDMETHODIMP_(int) OnExecuteProgress(
        __in_z LPCWSTR wzPackageId,
        __in DWORD dwProgressPercentage,
        __in DWORD dwOverallPercentage) override;
    STDMETHODIMP_(int) OnProgress(
        __in DWORD dwProgressPercentage,
        __in DWORD dwOverallPercentage) override;
    STDMETHODIMP_(int) OnError(
        __in BOOTSTRAPPER_ERROR_TYPE errorType,
        __in_z_opt LPCWSTR wzPackageId,
        __in DWORD dwCode,
        __in_z_opt LPCWSTR wzError,
        __in DWORD uiFlags,
        __in DWORD cData,
        __in_ecount_z_opt(cData) LPCWSTR* rgwzData,
        __in int nRecommendation) override;
    STDMETHODIMP_(int) OnApplyComplete(
        __in HRESULT hrStatus,
        __in BOOTSTRAPPER_APPLY_RESTART restart) override;

    // Called on the UI thread (from the bridge).
    void RequestInstall();
    void RequestCancel();
    // Repair = honest reinstall through the external-pack flow.
    void RequestRepair();
    // Uninstall = record-driven removal on a worker thread.
    void RequestUninstall();
    // Legacy MSI Burn flow (Plan/Apply through the engine).
    void RequestInstallMsi();
    // Phase 2 external-pack flow (local source now, Firebase later):
    // downloads + verifies + extracts packs on a worker thread with
    // progress/cancel. Used when LUNAR_PACK_SOURCE is set; otherwise
    // the MSI Burn flow (RequestInstall) applies.
    void RequestInstallExternal();
    // Elevation boundary (static so BATest can prove it headless):
    // true when path is missing/unwritable (admin rights needed).
    static bool NeedsElevationForPath(const std::wstring& path);
    static bool CanWriteDir(const std::wstring& path);
    // Start Menu + Desktop shortcuts (IShellLink, icon from the
    // installed exe). Only called after successful extraction.
    static void CreateAppShortcuts(const std::wstring& installPath);
    // Testable core: explicit link directories (no shell folders).
    static void CreateAppShortcutsTo(const std::wstring& installPath,
                                     const std::wstring& startMenuDir,
                                     const std::wstring& desktopDir);
    // Full user-initiated shutdown (tray Exit / window close): cancels
    // owned work, tears everything down, terminates the process UI.
    void RequestShutdown();
    // Single-instance gate (one installer window; second launch
    // focuses it and quits).
    bool AcquireSingleInstance();
    bool HasEngine() const { return m_pEngine != nullptr; }
    bool IsStandalone() const { return m_pEngine == nullptr; }
    // Owned UI thread (standalone host joins it; Burn owns lifetime).
    HANDLE UiThread() const { return m_uiThread; }
    IBootstrapperEngine* Engine() const { return m_pEngine; }
    BOOTSTRAPPER_ACTION InitialAction() const { return m_initialAction; }

    // Engine-thread -> UI-thread marshal helper.
    void PostUi(UINT msg, WPARAM w = 0, LPARAM l = 0);

private:
    static LRESULT CALLBACK NotifyWndProc(HWND h, UINT m, WPARAM w,
                                          LPARAM l);
    static DWORD WINAPI UiThreadProc(LPVOID param);
    static DWORD WINAPI DownloadThreadProc(LPVOID param);
    static DWORD WINAPI UninstallThreadProc(LPVOID param);

    // Standalone post-show step (UI thread): detect an existing install
    // and switch to the maintenance screen when one is registered.
    void OnStandaloneReady();

    void OnUiStarted();
    void OnUiProgress(DWORD overall, const std::wstring& phase);
    void OnUiDownloadProgress(DWORD percent, const std::wstring& phase,
                              const std::wstring& detail);
    void OnUiError(DWORD code, const std::wstring& package,
                   const std::wstring& message);
    void OnUiApplyComplete(HRESULT hr);
    void WriteInstallJson();

    HINSTANCE m_hInstance;
    IBootstrapperEngine* m_pEngine; // not owned (engine lifetime)
    HWND m_hwndNotify;              // UI-thread message window
    DWORD m_uiThreadId;
    HANDLE m_uiReady; // signaled when the UI thread pumps messages
    HANDLE m_doneEvent; // signaled by OnShutdown
    BOOTSTRAPPER_ACTION m_initialAction;

    NativeInstallerWindow* m_ui; // owned, UI thread only (Direct2D)
    InstallerBridge* m_bridge;  // owned, UI thread only

    volatile LONG m_cancelRequested;
    LunarInstallState m_state;
    CRITICAL_SECTION m_lock;
    // Test-only auto-install trigger (see OnUiStarted).
    bool m_testAutoInstall = false;
    // Last progress snapshot (error notices reuse it as detail context).
    std::wstring m_lastPhase;
    std::wstring m_lastDetail;
    DWORD m_lastPercent = 0;
    // Owned download worker (never detached: joined on shutdown).
    HANDLE m_worker = nullptr;
    // Owned UI thread (never detached: joined by standalone host).
    HANDLE m_uiThread = nullptr;
    // Current user-visible flow (install vs uninstall completion).
    enum class Flow { Install, Uninstall };
    Flow m_flow = Flow::Install;
    // Process-lifetime controls.
    HANDLE m_singleMutex = nullptr;
    bool m_shuttingDown = false;
    bool m_engineShutdown = false;
};
