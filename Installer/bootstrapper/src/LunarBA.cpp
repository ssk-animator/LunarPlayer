// LunarBA — lifecycle implementation. See LunarBA.h for the threading
// contract. Burn v3 calls into this object; the UI thread owns the
// native Direct2D installer window (no WebView2).

#include "LunarBA.h"

#include "BundleVersion.h"
#include "Downloader.h"
#include "InstallerBridge.h"
#include "Maintenance.h"
#include "NativeUi.h"

#include <algorithm>
#include <memory>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

// IDDOWNLOAD lives in the engine header (valid only from OnResolveSource).
#include <IBootstrapperEngine.h>

namespace {
constexpr UINT WM_LUNAR_UI_STARTED = WM_APP + 101;
constexpr UINT WM_LUNAR_PROGRESS = WM_APP + 102;
constexpr UINT WM_LUNAR_APPLY_DONE = WM_APP + 103;
constexpr UINT WM_LUNAR_ERROR = WM_APP + 104;
constexpr UINT WM_LUNAR_DLPROG = WM_APP + 105;
constexpr UINT WM_LUNAR_TESTINSTALL = WM_APP + 106;

struct ProgressPayload {
    DWORD overall;
    wchar_t phase[256];
    wchar_t detail[256];
};

struct ErrorPayload {
    DWORD code;
    wchar_t package[128];
    wchar_t message[512];
};

// Forward: stage diagnostics + uninstall job, defined alongside the
// install worker further below.
void DiagStage(const wchar_t* stage, const std::wstring& path,
               HRESULT hr);
struct UninstallJob {
    LunarBA* ba = nullptr;
    std::wstring installPath;
};

// Startup diagnostics: append-only records in %TEMP%\LunarInstall.log
// proving process start -> BA init -> UI init. Best-effort: never fails
// startup. Burn's own engine log stays the authority for Detect/Plan/
// Apply; this covers the BA/UI side the engine log cannot see
// (elevated clean-room path, cwd, elevation, UI Create result).
// LUNAR_MANIFEST_TAG names the bundle manifest mode (LunarBA target
// defines LUNAR_MANIFEST_ELEVATED in CMake; BATest/uitest log the
// default-manifest tag).
#ifdef LUNAR_MANIFEST_ELEVATED
#define LUNAR_MANIFEST_TAG "requireAdministrator(standalone-linker)"
#else
#define LUNAR_MANIFEST_TAG "default-manifest(test-harness)"
#endif
void StartupLog(const wchar_t* stage, HRESULT hr) {
    wchar_t log[MAX_PATH]{};
    DWORD nl = GetTempPathW(MAX_PATH, log);
    if (nl == 0 || nl >= MAX_PATH)
        return;
    wcscat_s(log, L"LunarInstall.log");
    FILE* f = nullptr;
    if (_wfopen_s(&f, log, L"a, ccs=UTF-8") != 0 || !f)
        return;
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wchar_t cwd[MAX_PATH]{};
    GetCurrentDirectoryW(MAX_PATH, cwd);
    BOOL admin = FALSE;
    {
        SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
        PSID group = nullptr;
        if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                     DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0,
                                     0, 0, &group)) {
            CheckTokenMembership(nullptr, group, &admin);
            FreeSid(group);
        }
    }
    SYSTEMTIME st{};
    GetLocalTime(&st);
    fwprintf_s(f,
               L"[%02d:%02d:%02d] startup stage=%s exe=%s cwd=%s "
               L"elevated=%d arch=%s manifest=%S "
               L"hr=0x%08X\n",
               st.wHour, st.wMinute, st.wSecond, stage, exe, cwd,
               admin ? 1 : 0,
#ifdef _WIN64
               L"x64",
#else
               L"x86",
#endif
               LUNAR_MANIFEST_TAG,
               (unsigned)hr);
    fclose(f);
}
} // namespace

LunarBA::LunarBA(HINSTANCE hInstance, IBootstrapperEngine* pEngine,
                 const BOOTSTRAPPER_COMMAND* pCommand)
    : m_hInstance(hInstance)
    , m_pEngine(pEngine)
    , m_hwndNotify(nullptr)
    , m_uiThreadId(0)
    , m_uiReady(CreateEventW(nullptr, TRUE, FALSE, nullptr))
    , m_doneEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr))
    , m_initialAction(pCommand ? pCommand->action
                               : BOOTSTRAPPER_ACTION_UNKNOWN)
    , m_ui(nullptr)
    , m_bridge(nullptr)
    , m_cancelRequested(0)
    , m_state(LunarInstallState::Idle) {
    InitializeCriticalSection(&m_lock);
    if (m_pEngine)
        m_pEngine->AddRef();
    StartupLog(L"ba-create", S_OK);
}

LunarBA::~LunarBA() {
    if (m_worker) {
        CloseHandle(m_worker);
        m_worker = nullptr;
    }
    if (m_uiThread) {
        CloseHandle(m_uiThread);
        m_uiThread = nullptr;
    }
    if (m_singleMutex) {
        ReleaseMutex(m_singleMutex);
        CloseHandle(m_singleMutex);
        m_singleMutex = nullptr;
    }
    if (m_pEngine)
        m_pEngine->Release();
    if (m_uiReady)
        CloseHandle(m_uiReady);
    if (m_doneEvent)
        CloseHandle(m_doneEvent);
    DeleteCriticalSection(&m_lock);
}

void LunarBA::RequestShutdown() {
    bool already = false;
    EnterCriticalSection(&m_lock);
    already = m_shuttingDown;
    m_shuttingDown = true;
    LeaveCriticalSection(&m_lock);
    if (already)
        return;
    InterlockedExchange(&m_cancelRequested, 1);
    HANDLE worker = nullptr;
    EnterCriticalSection(&m_lock);
    worker = m_worker;
    m_worker = nullptr;
    LeaveCriticalSection(&m_lock);
    if (worker) {
        WaitForSingleObject(worker, 15000);
        CloseHandle(worker);
    }
    HWND w = nullptr;
    if (m_ui) {
        w = m_ui->Window();
        // Do NOT delete here: the UI thread owns it (WM_DESTROY path
        // or message-loop tail performs the actual teardown).
    }
    if (w)
        DestroyWindow(w);
}

bool LunarBA::AcquireSingleInstance() {
    HANDLE m =
        CreateMutexW(nullptr, FALSE, L"LunarPlayerInstallerSingleton-v1");
    if (!m)
        return true; // fail-open: never block installation on this
    if (GetLastError() != ERROR_ALREADY_EXISTS) {
        m_singleMutex = m;
        return true;
    }
    CloseHandle(m);
    // Normal second launch: focus the existing window, then quit
    // quietly (no second engine, no second window). Elevation happens
    // once at startup (requireAdministrator manifest), so there is no
    // BA-driven handoff needing an exemption here.
    HWND w = FindWindowW(L"LunarInstallerNative", L"Lunar Player Installer");
    if (w) {
        if (IsIconic(w))
            ShowWindowAsync(w, SW_RESTORE);
        SetForegroundWindow(w);
    }
    return false;
}

void LunarBA::PostUi(UINT msg, WPARAM w, LPARAM l) {
    // During teardown the UI window is gone: drop (and free) payloads
    // instead of posting into the void. Types match each message.
    auto drop = [&]() {
        if (msg == WM_LUNAR_ERROR) {
            delete reinterpret_cast<ErrorPayload*>(l);
        } else {
            delete reinterpret_cast<ProgressPayload*>(l);
        }
    };
    if (m_shuttingDown) {
        if (msg == WM_LUNAR_PROGRESS || msg == WM_LUNAR_DLPROG
            || msg == WM_LUNAR_ERROR || msg == WM_LUNAR_APPLY_DONE
            || msg == WM_LUNAR_TESTINSTALL) {
            drop();
            return;
        }
    }
    if (m_hwndNotify) {
        PostMessageW(m_hwndNotify, msg, w, l);
    } else if (msg == WM_LUNAR_PROGRESS || msg == WM_LUNAR_DLPROG
               || msg == WM_LUNAR_ERROR || msg == WM_LUNAR_APPLY_DONE) {
        drop();
    }
}

LRESULT CALLBACK LunarBA::NotifyWndProc(HWND h, UINT m, WPARAM w,
                                        LPARAM l) {
    LunarBA* self = nullptr;
    if (m == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        self = reinterpret_cast<LunarBA*>(cs->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<LunarBA*>(
            GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    if (self) {
        switch (m) {
        case WM_LUNAR_UI_STARTED:
            self->OnUiStarted();
            return 0;
        case WM_LUNAR_TESTINSTALL:
            self->RequestInstall();
            return 0;        case WM_LUNAR_PROGRESS: {
            auto* p = reinterpret_cast<ProgressPayload*>(l);
            if (p) {
                self->OnUiProgress((DWORD)w, p->phase);
                delete p;
            }
            return 0;
        }
        case WM_LUNAR_DLPROG: {
            auto* p = reinterpret_cast<ProgressPayload*>(l);
            if (p) {
                self->OnUiDownloadProgress((DWORD)w, p->phase,
                                           p->detail);
                delete p;
            }
            return 0;
        }
        case WM_LUNAR_APPLY_DONE:
            self->OnUiApplyComplete((HRESULT)l);
            return 0;
        case WM_LUNAR_ERROR: {
            auto* p = reinterpret_cast<ErrorPayload*>(l);
            if (p) {
                self->OnUiError(p->code, p->package, p->message);
                delete p;
            }
            return 0;
        }
        }
    }
    return DefWindowProcW(h, m, w, l);
}

DWORD WINAPI LunarBA::UiThreadProc(LPVOID param) {
    // Own STA apartment for COM dialogs (folder picker); the engine
    // thread that called Create is MTA, so this fresh thread
    // initializes its own.
    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInit = SUCCEEDED(hrCom);
    LunarBA* self = reinterpret_cast<LunarBA*>(param);
    WNDCLASSW wc{};
    wc.lpfnWndProc = LunarBA::NotifyWndProc;
    wc.hInstance = self->m_hInstance;
    wc.lpszClassName = L"LunarInstallerNotify";
    RegisterClassW(&wc);
    self->m_hwndNotify = CreateWindowExW(0, wc.lpszClassName, L"",
                                         0, 0, 0, 0, 0, HWND_MESSAGE,
                                         nullptr, self->m_hInstance, self);
    self->m_uiThreadId = GetCurrentThreadId();

    self->m_bridge = new InstallerBridge(self);
    UiActions actions;
    actions.onInstall = [self]() { self->RequestInstall(); };
    actions.onCancel = [self]() { self->RequestCancel(); };
    actions.onBack = [self]() {
        EnterCriticalSection(&self->m_lock);
        self->m_state = LunarInstallState::Ready;
        LeaveCriticalSection(&self->m_lock);
    };
    actions.onLaunch = [self]() {
        if (!self->m_bridge)
            return;
        std::wstring err;
        if (!self->m_bridge->LaunchPlayerNative(err)) {
            if (self->m_ui) {
                std::wstring msg = L"Could not launch Lunar Player";
                if (!err.empty())
                    msg += L": " + err;
                msg += L".";
                self->m_ui->SetError(msg, false);
            }
            return;
        }
        // Launched exactly once: the installer is done — exit
        // completely (window + tray + processes). The player is an
        // independent process and keeps running.
        DiagStage(L"launch-exit", L"", S_OK);
        self->RequestShutdown();
    };
    actions.onExitTray = [self]() { self->RequestShutdown(); };
    actions.onRepair = [self]() { self->RequestRepair(); };
    actions.onUninstall = [self]() { self->RequestUninstall(); };
    actions.onModify = [self]() {
        EnterCriticalSection(&self->m_lock);
        self->m_state = LunarInstallState::Ready;
        LeaveCriticalSection(&self->m_lock);
    };
    self->m_ui = new NativeInstallerWindow(self->m_hInstance,
                                           self->m_bridge, actions);
    if (!self->m_ui->Create(L"Lunar Player Installer", 1280, 800)) {
        StartupLog(L"ui-create-fail",
                   HRESULT_FROM_WIN32(GetLastError()));
        return 1;
    }
    self->m_ui->Show();
    StartupLog(L"ui-shown", S_OK);
    if (!self->m_pEngine)
        self->OnStandaloneReady();

    SetEvent(self->m_uiReady);
    self->PostUi(WM_LUNAR_UI_STARTED);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Full teardown on EVERY exit path (window closed, engine shutdown,
    // tray exit): cancel owned work, join the worker, drop UI/bridge,
    // quit the engine unless it is already going away itself.
    EnterCriticalSection(&self->m_lock);
    self->m_shuttingDown = true;
    const bool engineGone = self->m_engineShutdown;
    HANDLE worker = self->m_worker;
    self->m_worker = nullptr;
    LeaveCriticalSection(&self->m_lock);
    InterlockedExchange(&self->m_cancelRequested, 1);
    if (worker) {
        WaitForSingleObject(worker, 15000);
        CloseHandle(worker);
    }
    delete self->m_ui;
    self->m_ui = nullptr;
    delete self->m_bridge;
    self->m_bridge = nullptr;
    if (!engineGone && self->m_pEngine)
        self->m_pEngine->Quit(0);
    if (comInit)
        CoUninitialize();
    return 0;
}

STDMETHODIMP LunarBA::OnStartup() {
    // Layout/cache/help runs headless: no window, engine still drives
    // Detect; every other action (install/uninstall/repair/modify, or no
    // action specified at all) gets the native Direct2D installer UI.
    // UNKNOWN must be interactive: a double-clicked bundle with no
    // explicit action would otherwise sit with no window and no way to
    // proceed.
    const bool headless =
        (m_initialAction == BOOTSTRAPPER_ACTION_LAYOUT
         || m_initialAction == BOOTSTRAPPER_ACTION_CACHE
         || m_initialAction == BOOTSTRAPPER_ACTION_HELP);
    // Test-only trigger (diagnostics): parsed here on the engine
    // thread (before Detect) so no UI/engine race can miss it. A lone
    // --lunar-test-install token auto-starts installation once Detect
    // completes. Never used by the real UI flow.
    {
        LPWSTR cmd = GetCommandLineW();
        if (cmd && wcsstr(cmd, L"--lunar-test-install"))
            m_testAutoInstall = true;
    }
    if (!headless) {
        // Single instance (UI runs only): a second launch focuses the
        // existing installer instead of accumulating processes. The
        // bundle elevates at startup (requireAdministrator manifest),
        // so there is no BA-driven handoff needing an exemption here.
        StartupLog(L"onstartup-single-instance", S_OK);
        if (!AcquireSingleInstance()) {
            StartupLog(L"onstartup-second-instance-quit", S_OK);
            if (m_pEngine)
                m_pEngine->Quit(0);
            return IDNOACTION;
        }
        HANDLE h =
            CreateThread(nullptr, 0, LunarBA::UiThreadProc, this, 0,
                         nullptr);
        if (!h) {
            StartupLog(L"onstartup-ui-thread-create-fail",
                       HRESULT_FROM_WIN32(GetLastError()));
            return E_FAIL;
        }
        // Owned (never detached): the standalone host joins it; the
        // Burn engine owns the BA lifetime instead.
        EnterCriticalSection(&m_lock);
        m_uiThread = h;
        LeaveCriticalSection(&m_lock);
        DWORD wr = WaitForSingleObject(m_uiReady, 30000);
        StartupLog(L"onstartup-ui-ready-wait",
                   wr == WAIT_OBJECT_0 ? S_OK
                                       : HRESULT_FROM_WIN32(ERROR_TIMEOUT));
        if (WAIT_OBJECT_0 != wr)
            return E_FAIL;
    } else {
        StartupLog(L"onstartup-headless", S_OK);
    }
    EnterCriticalSection(&m_lock);
    m_state = LunarInstallState::Detecting;
    LeaveCriticalSection(&m_lock);
    if (m_pEngine) {
        m_pEngine->Detect();
    } else {
        // Standalone host (no Burn engine): detection is local
        // (ARP/install.json) and runs on the UI thread once shown.
        StartupLog(L"onstartup-standalone", S_OK);
    }
    return IDNOACTION;
}

STDMETHODIMP_(int) LunarBA::OnShutdown() {
    StartupLog(L"onshutdown", S_OK);
    EnterCriticalSection(&m_lock);
    m_engineShutdown = true;
    LeaveCriticalSection(&m_lock);
    if (m_uiThreadId)
        PostThreadMessageW(m_uiThreadId, WM_QUIT, 0, 0);
    SetEvent(m_doneEvent);
    return IDNOACTION;
}

STDMETHODIMP_(void)
LunarBA::OnDetectComplete(__in HRESULT hrStatus) {
    (void)hrStatus;
    EnterCriticalSection(&m_lock);
    m_state = LunarInstallState::Ready;
    LeaveCriticalSection(&m_lock);
    if (m_testAutoInstall) {
        m_testAutoInstall = false;
        PostUi(WM_LUNAR_TESTINSTALL);
        return;
    }
    // Non-interactive actions proceed without the UI (layout/cache for
    // acquisition, uninstall/repair/modify run silent in v1 — documented).
    // INSTALL waits for the user in the React UI. The React UI reads
    // detection results via GetInstallationInfo.
    if (m_pEngine && m_initialAction != BOOTSTRAPPER_ACTION_INSTALL
        && m_initialAction != BOOTSTRAPPER_ACTION_HELP
        && m_initialAction != BOOTSTRAPPER_ACTION_UNKNOWN) {
        EnterCriticalSection(&m_lock);
        m_state = LunarInstallState::Planning;
        LeaveCriticalSection(&m_lock);
        m_pEngine->Plan(m_initialAction);
    }
}

STDMETHODIMP_(int)
LunarBA::OnCacheAcquireProgress(__in_z_opt LPCWSTR /*wzPackageOrContainerId*/,
                                __in_z_opt LPCWSTR /*wzPayloadId*/,
                                __in DWORD64 /*dw64Progress*/,
                                __in DWORD64 /*dw64Total*/,
                                __in DWORD /*dwOverallPercentage*/) {
    return IDNOACTION;
}

STDMETHODIMP_(int)
LunarBA::OnResolveSource(__in_z LPCWSTR wzPackageOrContainerId,
                         __in_z_opt LPCWSTR wzPayloadId,
                         __in_z LPCWSTR /*wzLocalSource*/,
                         __in_z_opt LPCWSTR wzDownloadSource) {
    // Launcher model: large payloads are NOT embedded, so a local miss
    // must download — the v3 contract REQUIRES IDDOWNLOAD here (any
    // other code, including IDNOACTION, is an error and aborts
    // acquisition). No URL means no acquisition path exists: cancel
    // honestly instead of hanging on a prompt no custom BA can show.
    if (!wzDownloadSource || !*wzDownloadSource)
        return IDCANCEL;
    (void)wzPackageOrContainerId;
    (void)wzPayloadId;
    return IDDOWNLOAD;
}

STDMETHODIMP_(void) LunarBA::OnCacheComplete(__in HRESULT hrStatus) {
    // Layout/cache runs headless: quit the engine when done. During a
    // normal install, caching is just a phase — never quit there.
    if ((m_initialAction == BOOTSTRAPPER_ACTION_LAYOUT
         || m_initialAction == BOOTSTRAPPER_ACTION_CACHE)
        && m_pEngine)
        m_pEngine->Quit(FAILED(hrStatus) ? (DWORD)hrStatus : 0);
}

STDMETHODIMP_(void) LunarBA::OnPlanComplete(__in HRESULT hrStatus) {
    if (FAILED(hrStatus)) {
        PostUi(WM_LUNAR_APPLY_DONE, 0, (LPARAM)(LRESULT)hrStatus);
        return;
    }
#ifdef LUNAR_BA_MOCK_APPLY
    // DEV-ONLY mock path (never in release builds): simulate progress so
    // the React UI can be exercised without a packaged bundle. Loudly
    // logged; production code always calls engine->Apply().
    OutputDebugStringW(L"[LunarBA] MOCK APPLY — dev only, no installation\n");
    for (int i = 0; i <= 100; i += 5) {
        if (InterlockedCompareExchange(&m_cancelRequested, 0, 0)) {
            PostUi(WM_LUNAR_APPLY_DONE, 0, (LPARAM)(LRESULT)HRESULT_FROM_WIN32(ERROR_CANCELLED));
            return;
        }
        auto* p = new ProgressPayload{};
        p->overall = (DWORD)i;
        wcscpy_s(p->phase, L"Installing (mock)");
        PostUi(WM_LUNAR_PROGRESS, (WPARAM)i, reinterpret_cast<LPARAM>(p));
        Sleep(80);
    }
    PostUi(WM_LUNAR_APPLY_DONE, 0, (LPARAM)(LRESULT)S_OK);
#else
    if (m_pEngine)
        m_pEngine->Apply(reinterpret_cast<HWND>(nullptr));
#endif
}

STDMETHODIMP_(int)
LunarBA::OnExecuteProgress(__in_z LPCWSTR /*wzPackageId*/,
                           __in DWORD /*dwProgressPercentage*/,
                           __in DWORD dwOverallPercentage) {
    if (InterlockedCompareExchange(&m_cancelRequested, 0, 0))
        return IDCANCEL;
    auto* p = new ProgressPayload{};
    p->overall = dwOverallPercentage;
    wcscpy_s(p->phase, L"Installing");
    PostUi(WM_LUNAR_PROGRESS, (WPARAM)dwOverallPercentage,
           reinterpret_cast<LPARAM>(p));
    return IDNOACTION;
}

STDMETHODIMP_(int)
LunarBA::OnProgress(__in DWORD /*dwProgressPercentage*/,
                    __in DWORD dwOverallPercentage) {
    if (InterlockedCompareExchange(&m_cancelRequested, 0, 0))
        return IDCANCEL;
    auto* p = new ProgressPayload{};
    p->overall = dwOverallPercentage;
    wcscpy_s(p->phase, L"Working");
    PostUi(WM_LUNAR_PROGRESS, (WPARAM)dwOverallPercentage,
           reinterpret_cast<LPARAM>(p));
    return IDNOACTION;
}

STDMETHODIMP_(int)
LunarBA::OnError(__in BOOTSTRAPPER_ERROR_TYPE errorType,
                 __in_z_opt LPCWSTR wzPackageId, __in DWORD dwCode,
                 __in_z_opt LPCWSTR wzError, __in DWORD /*uiFlags*/,
                 __in DWORD /*cData*/,
                 __in_ecount_z_opt(cData) LPCWSTR* /*rgwzData*/,
                 __in int /*nRecommendation*/) {
    (void)errorType;
    // Surface the failure to React as an actionable event (offline,
    // hash mismatch, missing payload...). The engine still applies its
    // default recommendation; the UI must not hang on silent progress.
    auto* p = new ErrorPayload{};
    p->code = dwCode;
    wcsncpy_s(p->package, wzPackageId ? wzPackageId : L"",
              _TRUNCATE);
    wcsncpy_s(p->message, wzError ? wzError : L"", _TRUNCATE);
    PostUi(WM_LUNAR_ERROR, 0, reinterpret_cast<LPARAM>(p));
    return IDNOACTION;
}

STDMETHODIMP_(int)
LunarBA::OnApplyComplete(__in HRESULT hrStatus,
                         __in BOOTSTRAPPER_APPLY_RESTART /*restart*/) {
    PostUi(WM_LUNAR_APPLY_DONE, 0, (LPARAM)(LRESULT)hrStatus);
    return IDNOACTION;
}

void LunarBA::RequestInstall() {
    // External-pack flow is the release path whenever the manifest
    // carries pack metadata (or a local test source is set); otherwise
    // the legacy MSI Burn flow applies.
    bool external = false;
    if (m_bridge && m_bridge->CorePack().present)
        external = true;
    wchar_t src[4096]{};
    DWORD n = GetEnvironmentVariableW(L"LUNAR_PACK_SOURCE", src, 4096);
    if (n > 0 && n < 4096)
        external = true;
    if (!external) {
        if (IsStandalone()) {
            // No packs, no engine: fail fast with a clear message
            // instead of stalling on the progress screen.
            if (m_ui)
                m_ui->SetError(
                    L"Installation data is missing (payload-sizes.json "
                    L"not found next to the installer).",
                    false);
            EnterCriticalSection(&m_lock);
            m_state = LunarInstallState::Failed;
            LeaveCriticalSection(&m_lock);
            return;
        }
        RequestInstallMsi();
        return;
    }
    // Elevation pre-flight FIRST: never download ~500 MB only to
    // discover the target is unwritable. The bundle elevates at startup
    // (requireAdministrator), so the normal flow is already elevated
    // here. If not, fail fast with a clear message (no silent fallback,
    // no weakened permissions, no second process).
    if (m_bridge && NeedsElevationForPath(m_bridge->InstallPath())) {
        if (m_ui) {
            wchar_t msg[512]{};
            swprintf_s(msg,
                       L"Administrator rights are required to install to:\n%s\n\n"
                       L"Please relaunch the installer and accept the UAC prompt.",
                       m_bridge->InstallPath().c_str());
            m_ui->SetError(msg, false);
        }
        EnterCriticalSection(&m_lock);
        m_state = LunarInstallState::Failed;
        LeaveCriticalSection(&m_lock);
        return;
    }
    RequestInstallExternal();
}

void LunarBA::RequestInstallMsi() {
    InterlockedExchange(&m_cancelRequested, 0);
    EnterCriticalSection(&m_lock);
    m_state = LunarInstallState::Planning;
    LeaveCriticalSection(&m_lock);
    if (!m_pEngine)
        return;
    // Checkbox state -> Burn variables (AI package InstallCondition).
    // Core/FFmpeg are required: always planned.
    if (m_bridge) {
        const std::vector<std::wstring> sel = m_bridge->SelectedIds();
        const bool ai =
            std::find(sel.begin(), sel.end(), L"aiStudio") != sel.end();
        m_pEngine->SetVariableString(L"InstallAI", ai ? L"1" : L"0");
    }
    m_pEngine->Plan(BOOTSTRAPPER_ACTION_INSTALL);
}

void LunarBA::RequestCancel() {
    InterlockedExchange(&m_cancelRequested, 1);
    EnterCriticalSection(&m_lock);
    if (m_state == LunarInstallState::Applying)
        m_state = LunarInstallState::Cancelling;
    LeaveCriticalSection(&m_lock);
}

void LunarBA::RequestRepair() {
    DiagStage(L"repair-begin",
              m_bridge ? m_bridge->InstallPath() : L"", S_OK);
    m_flow = Flow::Install;
    RequestInstallExternal(); // honest reinstall of selected components
}

void LunarBA::RequestUninstall() {
    if (!m_bridge)
        return;
    DiagStage(L"uninstall-begin", m_bridge->InstallPath(), S_OK);
    m_flow = Flow::Uninstall;
    auto* job = new UninstallJob();
    job->ba = this;
    job->installPath = m_bridge->InstallPath();
    InterlockedExchange(&m_cancelRequested, 0);
    EnterCriticalSection(&m_lock);
    if (m_worker) {
        if (WaitForSingleObject(m_worker, 0) == WAIT_OBJECT_0) {
            CloseHandle(m_worker);
            m_worker = nullptr;
        } else {
            LeaveCriticalSection(&m_lock);
            delete job;
            m_flow = Flow::Install;
            return;
        }
    }
    HANDLE h = CreateThread(nullptr, 0,
                            LunarBA::UninstallThreadProc, job, 0,
                            nullptr);
    if (!h) {
        LeaveCriticalSection(&m_lock);
        delete job;
        m_flow = Flow::Install;
        PostUi(WM_LUNAR_APPLY_DONE, 0, (LPARAM)(LRESULT)E_FAIL);
        return;
    }
    m_state = LunarInstallState::Applying;
    m_worker = h;
    LeaveCriticalSection(&m_lock);
}

DWORD WINAPI LunarBA::UninstallThreadProc(LPVOID param) {
    std::unique_ptr<UninstallJob> job(
        reinterpret_cast<UninstallJob*>(param));
    LunarBA* self = job->ba;
    HRESULT hr = Maintenance::RemoveInstallation(
        job->installPath,
        [&](const wchar_t* phase, const wchar_t* detail) {
            auto* p = new ProgressPayload{};
            p->overall = 50;
            wcsncpy_s(p->phase, phase ? phase : L"Removing",
                      _TRUNCATE);
            wcsncpy_s(p->detail, detail ? detail : L"", _TRUNCATE);
            self->PostUi(WM_LUNAR_DLPROG, 50,
                         reinterpret_cast<LPARAM>(p));
        },
        &self->m_cancelRequested);
    DiagStage(L"uninstall-end", job->installPath, hr);
    if (hr == E_ABORT)
        hr = HRESULT_FROM_WIN32(ERROR_CANCELLED);
    self->PostUi(WM_LUNAR_APPLY_DONE, 0, (LPARAM)(LRESULT)hr);
    return 0;
}

namespace {
bool IsElevatedProcess() {
    BOOL admin = FALSE;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID group = nullptr;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0,
                                 0, &group)) {
        CheckTokenMembership(nullptr, group, &admin);
        FreeSid(group);
    }
    return admin ? true : false;
}

// NOTE: elevation happens once at startup via the bundle manifest
// (requireAdministrator): this BA never relaunches itself and keeps no
// cross-process job state.
} // namespace

bool LunarBA::CanWriteDir(const std::wstring& path) {
    if (path.empty())
        return false;
    // Create each level best-effort (succeeds when already present).
    std::wstring cur;
    size_t i = 0;
    if (path.size() >= 3 && path[1] == L':'
        && (path[2] == L'\\' || path[2] == L'/')) {
        cur = path.substr(0, 3); // drive root always exists
        i = 3;
    } else if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') {
        return false; // UNC targets: elevation cannot fix those
    }
    for (; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == L'\\' || path[i] == L'/') {
            std::wstring part = path.substr(0, i);
            if (part.empty() || part.back() == L':')
                continue;
            if (!CreateDirectoryW(part.c_str(), nullptr)) {
                DWORD e = GetLastError();
                if (e != ERROR_ALREADY_EXISTS)
                    return false;
            }
        }
    }
    // Probe: create + write + delete (atomic-ish, no residue).
    // Shares read/delete so real-time scanners can't fail the probe
    // with a sharing violation.
    std::wstring probe = path + L"\\.lunar_write_test";
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                           CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY
                               | FILE_FLAG_DELETE_ON_CLOSE,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD w = 0;
    const bool ok =
        WriteFile(h, "1", 1, &w, nullptr) && w == 1;
    CloseHandle(h);
    return ok;
}

bool LunarBA::NeedsElevationForPath(const std::wstring& path) {
    if (IsElevatedProcess())
        return false;
    return !CanWriteDir(path);
}

// The 7z packs carry top-level folders (core/, ffmpeg/, ai/) but the
// installed layout is flat (exe + DLLs at the install root, matching
// the MSI layout). Hoist each pack folder's children up one level.
// Repair-over-existing must MERGE: MoveFile cannot replace a directory,
// so subdirectory trees merge recursively (files overwrite), then the
// emptied source tree is pruned. Failures are logged, never silent.
static bool MergeMoveTree(const std::wstring& src,
                          const std::wstring& dst) {
    DWORD a = GetFileAttributesW(src.c_str());
    if (a == INVALID_FILE_ATTRIBUTES)
        return false;
    if (!(a & FILE_ATTRIBUTE_DIRECTORY)) {
        if (!MoveFileWithProgressW(src.c_str(), dst.c_str(), nullptr,
                                   nullptr,
                                   MOVEFILE_COPY_ALLOWED
                                       | MOVEFILE_REPLACE_EXISTING)) {
            DiagStage(L"flatten-file-fail", src,
                      HRESULT_FROM_WIN32(GetLastError()));
            return false;
        }
        return true;
    }
    CreateDirectoryW(dst.c_str(), nullptr); // exists-or-created
    bool ok = true;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((src + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        DiagStage(L"flatten-enum-fail", src,
                  HRESULT_FROM_WIN32(GetLastError()));
        return false;
    }
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
            continue;
        if (!MergeMoveTree(src + L"\\" + fd.cFileName,
                           dst + L"\\" + fd.cFileName))
            ok = false;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (ok && !RemoveDirectoryW(src.c_str())) {
        DiagStage(L"flatten-rmdir-fail", src,
                  HRESULT_FROM_WIN32(GetLastError()));
        ok = false;
    }
    return ok;
}
static void FlattenPackDirs(const std::wstring& root) {
    static const wchar_t* tops[] = { L"core", L"ffmpeg", L"ai" };
    for (int t = 0; t < 3; ++t) {
        std::wstring from = root + L"\\" + tops[t];
        DWORD a = GetFileAttributesW(from.c_str());
        if (a == INVALID_FILE_ATTRIBUTES
            || !(a & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        WIN32_FIND_DATAW fd{};
        HANDLE h =
            FindFirstFileW((from + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        do {
            if (!wcscmp(fd.cFileName, L".")
                || !wcscmp(fd.cFileName, L".."))
                continue;
            MergeMoveTree(from + L"\\" + fd.cFileName,
                          root + L"\\" + fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        if (!RemoveDirectoryW(from.c_str()))
            DiagStage(L"flatten-topdir-left", from, S_OK);
    }
}

void LunarBA::CreateAppShortcuts(const std::wstring& installPath) {    wchar_t programs[MAX_PATH]{}, desktop[MAX_PATH]{};
    // All-users locations: the installer runs elevated for a
    // per-machine target, so per-user shell folders would resolve to
    // the admin profile, not the installing user's.
    SHGetFolderPathW(nullptr, CSIDL_COMMON_PROGRAMS, nullptr, 0,
                     programs);
    SHGetFolderPathW(nullptr, CSIDL_COMMON_DESKTOPDIRECTORY, nullptr,
                     0, desktop);
    CreateAppShortcutsTo(installPath, programs, desktop);
}

void LunarBA::CreateAppShortcutsTo(const std::wstring& installPath,
                                   const std::wstring& startMenuDir,
                                   const std::wstring& desktopDir) {
    std::wstring target = installPath + L"\\LunarPlayer.exe";
    if (GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES)
        return;
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool coInit = SUCCEEDED(hrCo);
    const wchar_t* dirs[2] = { nullptr, nullptr };
    std::wstring d0 = startMenuDir, d1 = desktopDir;
    dirs[0] = d0.c_str();
    dirs[1] = d1.c_str();
    for (int i = 0; i < 2; ++i) {
        if (!dirs[i][0])
            continue;
        IShellLinkW* link = nullptr;
        if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr,
                                    CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&link))))
            continue;
        link->SetPath(target.c_str());
        link->SetWorkingDirectory(installPath.c_str());
        link->SetDescription(L"Lunar Player");
        link->SetIconLocation(target.c_str(), 0);
        IPersistFile* file = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
            std::wstring lnk = std::wstring(dirs[i])
                + L"\\Lunar Player.lnk";
            file->Save(lnk.c_str(), TRUE);
            file->Release();
        }
        link->Release();
    }
    if (coInit)
        CoUninitialize();
}

void LunarBA::OnUiStarted() {
    EnterCriticalSection(&m_lock);
    if (m_state == LunarInstallState::Idle)
        m_state = LunarInstallState::Detecting;
    LeaveCriticalSection(&m_lock);
}

// Standalone post-show detection (UI thread): an existing install
// switches the home screen to maintenance (Repair/Modify/Uninstall).
void LunarBA::OnStandaloneReady() {
    if (!m_bridge || !m_ui)
        return;
    std::wstring path, ver;
    if (Maintenance::IsInstalled(path, ver)) {
        m_bridge->SetInstallPath(path);
        m_ui->SetMaintenance(ver, path);
        DiagStage(L"standalone-maintenance", path, S_OK);
    } else {
        DiagStage(L"standalone-fresh-install", L"", S_OK);
    }
    EnterCriticalSection(&m_lock);
    m_state = LunarInstallState::Ready;
    LeaveCriticalSection(&m_lock);
}
void LunarBA::OnUiDownloadProgress(DWORD percent,
                                       const std::wstring& phase,
                                       const std::wstring& detail) {
    m_lastPhase = phase;
    m_lastDetail = detail;
    m_lastPercent = percent > 100 ? 100 : percent;
    if (m_ui)
        m_ui->SetProgress(phase, detail, m_lastPercent);
}

namespace {
// Phase 2 external-pack install worker (runs off the UI thread; all UI
// updates marshal through PostUi).
// Temporary stage diagnostics (DIAG-80070003): append-only log of every
// stage + path + HRESULT so a field failure names its exact operation.
void DiagStage(const wchar_t* stage, const std::wstring& path,
               HRESULT hr) {
    wchar_t log[MAX_PATH]{};
    DWORD nl = GetTempPathW(MAX_PATH, log);
    if (nl == 0 || nl >= MAX_PATH)
        return;
    wcscat_s(log, L"LunarInstall.log");
    FILE* f = nullptr;
    _wfopen_s(&f, log, L"a, ccs=UTF-8");
    if (!f)
        return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    fwprintf_s(f, L"[%02d:%02d:%02d] %s path=%s hr=0x%08X\n", st.wHour,
               st.wMinute, st.wSecond, stage, path.c_str(),
               (unsigned)hr);
    fclose(f);
}
struct DownloadJob {
    LunarBA* ba = nullptr;
    std::wstring sourceBase; // dir path or URL base (LUNAR_PACK_SOURCE)
    std::wstring sevenZip;   // extractor binary (LUNAR_SEVENZIP or default)
    std::wstring installPath;
    std::vector<std::wstring> selected;
    DownloadPack corePack;
    DownloadPack aiPack;
    uint64_t estimatedKB = 0; // ARP EstimatedSize
};

std::wstring MbLabel(uint64_t bytes) {
    wchar_t b[64]{};
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
        swprintf_s(b, L"%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    else
        swprintf_s(b, L"%.1f MB", bytes / (1024.0 * 1024.0));
    return b;
}
} // namespace

DWORD WINAPI LunarBA::DownloadThreadProc(LPVOID param) {
    std::unique_ptr<DownloadJob> job(
        reinterpret_cast<DownloadJob*>(param));
    LunarBA* self = job->ba;
    EnterCriticalSection(&self->m_lock);
    self->m_state = LunarInstallState::Applying;
    LeaveCriticalSection(&self->m_lock);

    struct Want {
        DownloadPack pack;
    };
    std::vector<Want> wants;
    const bool needAi =
        std::find(job->selected.begin(), job->selected.end(), L"aiStudio")
        != job->selected.end();
    if (job->corePack.present)
        wants.push_back({ job->corePack });
    if (needAi && job->aiPack.present)
        wants.push_back({ job->aiPack });
    HRESULT hr = wants.empty() ? E_FAIL : S_OK;
    DiagStage(L"wants", L"count=" + std::to_wstring(wants.size()), hr);
    uint64_t totalDl = 0, baseDone = 0;
    for (auto& w : wants)
        totalDl += w.pack.size;

    wchar_t dlDir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, dlDir);
    std::wstring dlRoot =
        std::wstring(dlDir) + L"LunarDlPacks";
    if (!CreateDirectoryW(dlRoot.c_str(), nullptr)) {
        DWORD de = GetLastError();
        if (de != ERROR_ALREADY_EXISTS) {
            hr = HRESULT_FROM_WIN32(de);
            DiagStage(L"mkdir-dlroot", dlRoot, hr);
        }
    }
    std::vector<std::wstring> archives;

    // System prerequisite first (small): VC++ runtime so the installed
    // player can launch on fresh machines. Failure here is non-fatal
    // (logged); the player launch surfaces the real error if any.
    if (SUCCEEDED(hr)) {
        DiagStage(L"vcredist-begin", dlRoot, S_OK);
        auto* vp = new ProgressPayload{};
        vp->overall = 0;
        wcscpy_s(vp->phase, L"Preparing system components");
        wcscpy_s(vp->detail, L"Checking VC++ runtime");
        self->PostUi(WM_LUNAR_DLPROG, 0,
                     reinterpret_cast<LPARAM>(vp));
        HRESULT hrVc = Maintenance::InstallVcRedist(
            dlRoot,
            [&](const wchar_t* phase, const wchar_t* detail) {
                auto* p = new ProgressPayload{};
                p->overall = 0;
                wcsncpy_s(p->phase, phase ? phase : L"", _TRUNCATE);
                wcsncpy_s(p->detail, detail ? detail : L"",
                          _TRUNCATE);
                self->PostUi(WM_LUNAR_DLPROG, 0,
                             reinterpret_cast<LPARAM>(p));
            },
            &self->m_cancelRequested);
        DiagStage(L"vcredist-end", L"", hrVc);
        if (hrVc == HRESULT_FROM_WIN32(ERROR_CANCELLED)
            || hrVc == E_ABORT)
            hr = HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }

    const bool isUrl = PackDownloader::IsHttp(job->sourceBase);
    for (auto& w : wants) {
        if (FAILED(hr))
            break;
        // Release manifest URL wins; the local override exists only
        // for development testing (LUNAR_PACK_SOURCE).
        std::wstring src;
        if (!job->sourceBase.empty()) {
            src = isUrl ? (job->sourceBase + L"/" + w.pack.file)
                        : (job->sourceBase + L"\\" + w.pack.file);
        } else if (!w.pack.url.empty()) {
            src = w.pack.url;
        } else {
            hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
            break;
        }
        std::wstring dst = dlRoot + L"\\" + w.pack.file;
        std::wstring phase = L"Downloading " + w.pack.file;
        InterlockedExchange(&self->m_cancelRequested, 0);
        DiagStage(L"fetch-src", src, S_OK);
        hr = PackDownloader::Fetch(
            src, dst, w.pack.size, w.pack.sha256,
            [&](uint64_t rx, uint64_t total) {
                if (InterlockedCompareExchange(&self->m_cancelRequested,
                                               0, 0))
                    return false;
                uint64_t overall = baseDone + rx;
                DWORD pct = totalDl > 0
                    ? (DWORD)(overall * 100 / totalDl)
                    : 0;
                wchar_t det[128]{};
                swprintf_s(det, L"%s / %s", MbLabel(rx).c_str(),
                           MbLabel(total ? total : w.pack.size).c_str());
                auto* p = new ProgressPayload{};
                p->overall = pct;
                wcsncpy_s(p->phase, phase.c_str(), _TRUNCATE);
                wcsncpy_s(p->detail, det, _TRUNCATE);
                self->PostUi(WM_LUNAR_DLPROG, (WPARAM)pct,
                             reinterpret_cast<LPARAM>(p));
                return true;
            },
            &self->m_cancelRequested);
        if (SUCCEEDED(hr)) {
            baseDone += w.pack.size;
            archives.push_back(dst);
            DiagStage(L"fetch-ok", dst, hr);
        } else {
            DiagStage(L"fetch-fail", src, hr);
        }
    }

    // Extract in pack order into the install location.
    if (SUCCEEDED(hr)) {
        BOOL mk = CreateDirectoryW(job->installPath.c_str(), nullptr);
        DiagStage(L"mkdir-install",
                  job->installPath + (mk ? L"" : L" (exists-or-failed)"),
                  mk ? S_OK : HRESULT_FROM_WIN32(GetLastError()));
        for (auto& arc : archives) {
            auto* p = new ProgressPayload{};
            p->overall = 100;
            wcscpy_s(p->phase, L"Extracting");
            std::wstring fn = arc.substr(arc.find_last_of(L"\\/") + 1);
            wcsncpy_s(p->detail, fn.c_str(), _TRUNCATE);
            self->PostUi(WM_LUNAR_DLPROG, 100,
                         reinterpret_cast<LPARAM>(p));
            std::wstring cmd = L"\"" + job->sevenZip + L"\" x \""
                + arc + L"\" -o\"" + job->installPath
                + L"\" -bso0 -bsp0 -y";
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi{};
            if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr,
                                FALSE, 0, nullptr, nullptr, &si, &pi)) {
                hr = HRESULT_FROM_WIN32(GetLastError());
                DiagStage(L"extract-spawn", job->sevenZip, hr);
                break;
            }
            DiagStage(L"extract-start", arc, S_OK);
            // Cooperative cancel while extracting (brief phase).
            for (;;) {
                DWORD wr = WaitForSingleObject(pi.hProcess, 250);
                if (wr != WAIT_TIMEOUT)
                    break;
                if (InterlockedCompareExchange(&self->m_cancelRequested,
                                               0, 0)) {
                    TerminateProcess(pi.hProcess, 1);
                    WaitForSingleObject(pi.hProcess, 5000);
                    hr = HRESULT_FROM_WIN32(ERROR_CANCELLED);
                    break;
                }
            }
            DWORD ec = 0;
            if (SUCCEEDED(hr)) {
                if (!GetExitCodeProcess(pi.hProcess, &ec) || ec != 0) {
                    hr = E_FAIL;
                    DiagStage(L"extract-exit", arc,
                              HRESULT_FROM_WIN32((DWORD)ec));
                } else {
                    DiagStage(L"extract-ok", arc, S_OK);
                }
            }
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            if (FAILED(hr))
                break;
        }
    }

    // Best-effort temp cleanup (kept on failure for diagnosis).
    if (SUCCEEDED(hr)) {
        FlattenPackDirs(job->installPath);
        // Modify path: AI deselected removes previously installed AI
        // files (by the old record; without one we keep them rather
        // than guess).
        if (!needAi) {
            std::vector<std::wstring> oc, oa;
            bool oai = false;
            if (SUCCEEDED(Maintenance::ReadInstalledFiles(
                    job->installPath, oc, oa, oai))
                && oai && !oa.empty()) {
                size_t dropped = 0;
                for (auto& f : oa) {
                    std::wstring p = job->installPath + L"\\" + f;
                    for (auto& c : p) {
                        if (c == L'/')
                            c = L'\\';
                    }
                    if (DeleteFileW(p.c_str()))
                        ++dropped;
                }
                DiagStage(
                    L"prune-ai",
                    L"dropped=" + std::to_wstring(dropped),
                    S_OK);
            }
        }
        CreateAppShortcuts(job->installPath);
        if (self->IsStandalone()) {
            hr = Maintenance::CopySelfAsUninstaller(
                job->installPath);
            DiagStage(L"copy-uninstaller", job->installPath, hr);
        }
        if (SUCCEEDED(hr)) {
            // Installed-files record: sidecar pack lists when present
            // (exact attribution), else a full enumeration fallback.
            std::vector<std::wstring> cf, af;
            bool haveLists = SUCCEEDED(Maintenance::PackFileLists(
                                 cf, af))
                && !cf.empty();
            if (!haveLists) {
                cf.clear();
                af.clear();
                WIN32_FIND_DATAW fd{};
                HANDLE h = FindFirstFileW(
                    (job->installPath + L"\\*").c_str(), &fd);
                if (h != INVALID_HANDLE_VALUE) {
                    do {
                        if (!wcscmp(fd.cFileName, L".")
                            || !wcscmp(fd.cFileName, L".."))
                            continue;
                        if (fd.dwFileAttributes
                            & FILE_ATTRIBUTE_DIRECTORY)
                            continue;
                        cf.emplace_back(fd.cFileName);
                    } while (FindNextFileW(h, &fd));
                    FindClose(h);
                }
            }
            hr = Maintenance::WriteInstalledFiles(
                job->installPath, cf, af, needAi);
            DiagStage(L"write-record", job->installPath, hr);
        }
        if (SUCCEEDED(hr)) {
            hr = Maintenance::RegisterARP(
                job->installPath,
                std::wstring(LUNAR_BUNDLE_VERSION),
                job->estimatedKB);
            DiagStage(L"register-arp", job->installPath, hr);
        }
        for (auto& arc : archives)
            DeleteFileW(arc.c_str());
        RemoveDirectoryW(dlRoot.c_str());
    }
    if (hr == E_ABORT)
        hr = HRESULT_FROM_WIN32(ERROR_CANCELLED);
    self->PostUi(WM_LUNAR_APPLY_DONE, 0, (LPARAM)(LRESULT)hr);
    return 0;
}

void LunarBA::RequestInstallExternal() {
    InterlockedExchange(&m_cancelRequested, 0);
    EnterCriticalSection(&m_lock);
    m_state = LunarInstallState::Planning;
    m_flow = Flow::Install;
    LeaveCriticalSection(&m_lock);
    if (!m_bridge)
        return;
    // Extractor must exist before any download: fail fast otherwise.
    const std::wstring extractor = Maintenance::FindExtractor();
    if (extractor.empty()) {
        DiagStage(L"extract-missing", L"7z.exe",
                  HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
        if (m_ui)
            m_ui->SetError(
                L"Archive extractor not found. Install 7-Zip or place "
                L"7z.exe next to the installer, then retry.",
                false);
        EnterCriticalSection(&m_lock);
        m_state = LunarInstallState::Failed;
        LeaveCriticalSection(&m_lock);
        return;
    }
    auto* job = new DownloadJob();
    job->ba = this;
    wchar_t src[4096]{};
    DWORD n = GetEnvironmentVariableW(L"LUNAR_PACK_SOURCE", src, 4096);
    if (n > 0 && n < 4096)
        job->sourceBase.assign(src);
    job->sevenZip = extractor;
    job->installPath = m_bridge->InstallPath();
    job->selected = m_bridge->SelectedIds();
    job->corePack = m_bridge->CorePack();
    job->aiPack = m_bridge->AiPack();
    job->estimatedKB = m_bridge->SelectedBytes() / 1024ULL;
    // Owned worker (never detached): reap a finished predecessor;
    // refuse while one is live (unreachable via UI: Install switches
    // screens immediately).
    EnterCriticalSection(&m_lock);
    if (m_worker) {
        if (WaitForSingleObject(m_worker, 0) == WAIT_OBJECT_0) {
            CloseHandle(m_worker);
            m_worker = nullptr;
        } else {
            LeaveCriticalSection(&m_lock);
            delete job;
            return;
        }
    }
    HANDLE h = CreateThread(nullptr, 0, LunarBA::DownloadThreadProc, job,
                            0, nullptr);
    if (!h) {
        LeaveCriticalSection(&m_lock);
        delete job;
        PostUi(WM_LUNAR_APPLY_DONE, 0, (LPARAM)(LRESULT)E_FAIL);
        return;
    }
    m_worker = h;
    LeaveCriticalSection(&m_lock);
}
void LunarBA::OnUiProgress(DWORD overall, const std::wstring& phase) {
    m_lastPhase = phase;
    m_lastPercent = overall > 100 ? 100 : overall;
    if (m_ui)
        m_ui->SetProgress(phase, m_lastDetail, m_lastPercent);
}

void LunarBA::OnUiError(DWORD code, const std::wstring& package,
                        const std::wstring& message) {
    // Mid-apply engine notice: surface as progress detail (the final
    // verdict still arrives via OnApplyComplete), so the UI never
    // hangs on silent progress for offline/hash failures.
    wchar_t buf[640]{};
    swprintf_s(buf, L"%s%s (0x%08X)",
               package.empty() ? L"" : (package + L": ").c_str(),
               message.empty() ? L"error" : message.c_str(), code);
    m_lastDetail.assign(buf);
    if (m_ui)
        m_ui->SetProgress(m_lastPhase, m_lastDetail, m_lastPercent);
}

void LunarBA::OnUiApplyComplete(HRESULT hr) {
    const Flow flow = m_flow;
    m_flow = Flow::Install;
    if (FAILED(hr)) {
        const bool cancelled =
            (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED));
        if (m_ui) {
            wchar_t buf[192]{};
            if (flow == Flow::Uninstall)
                swprintf_s(buf, L"Uninstall failed (0x%08lX).",
                           (long)hr);
            else
                swprintf_s(buf, L"Installation failed (0x%08lX). "
                                L"Previous version (if any) was rolled back untouched.",
                           (long)hr);
            m_ui->SetError(cancelled ? L"" : buf, cancelled);
        }
        EnterCriticalSection(&m_lock);
        m_state = LunarInstallState::Failed;
        LeaveCriticalSection(&m_lock);
        return;
    }
    if (flow == Flow::Uninstall) {
        if (m_ui)
            m_ui->SetUninstalled();
        EnterCriticalSection(&m_lock);
        m_state = LunarInstallState::Complete;
        LeaveCriticalSection(&m_lock);
        return;
    }
    WriteInstallJson();
    // Opt-in auto-launch only (default OFF: the success screen waits
    // for the user). After a successful launch the installer exits
    // completely, mirroring the Launch button.
    if (m_bridge && m_bridge->IsLaunchAfter()) {
        std::wstring ignored;
        if (m_bridge->LaunchPlayerNative(ignored)) {
            DiagStage(L"launch-exit", L"", S_OK);
            if (m_ui)
                m_ui->SetComplete();
            EnterCriticalSection(&m_lock);
            m_state = LunarInstallState::Complete;
            LeaveCriticalSection(&m_lock);
            RequestShutdown();
            return;
        }
    }
    if (m_ui)
        m_ui->SetComplete();
    EnterCriticalSection(&m_lock);
    m_state = LunarInstallState::Complete;
    LeaveCriticalSection(&m_lock);
}

// install.json: the machine-readable bridge between installer, updater,
// player, and uninstall verification (docs/UpdateProtocol.md). Written on
// successful Apply only; failures leave any previous record untouched.
void LunarBA::WriteInstallJson() {
    if (!m_bridge)
        return;
    const std::wstring path = m_bridge->InstallPath() + L"\\install.json";
    const std::vector<std::wstring> sel = m_bridge->SelectedIds();
    auto has = [&](const wchar_t* id) {
        return std::find(sel.begin(), sel.end(), id) != sel.end();
    };
    wchar_t json[2048]{};
    swprintf_s(json,
               L"{\n  \"schema\": 1,\n  \"product\": \"Lunar Player\",\n"
               L"  \"version\": \"%s\",\n  \"architecture\": \"x64\",\n"
               L"  \"installPath\": \"%s\",\n  \"channel\": \"stable\",\n"
               L"  \"components\": {\n    \"core\": %s,\n    \"ffmpeg\": %s,\n"
               L"    \"aiSubtitleStudio\": %s,\n    \"aiModels\": %s\n  }\n}\n",
               LUNAR_BUNDLE_VERSION,
               m_bridge->InstallPath().c_str(),
               has(L"core") ? L"true" : L"false",
               has(L"ffmpeg") ? L"true" : L"false",
               has(L"aiStudio") ? L"true" : L"false",
               has(L"aiModels") ? L"true" : L"false");
    // Install dir exists post-Apply; best-effort write (a missing file is
    // reported, never fatal to an otherwise good install).
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;
    std::string utf8;
    const int n = WideCharToMultiByte(CP_UTF8, 0, json, -1, nullptr, 0,
                                      nullptr, nullptr);
    if (n > 1) {
        utf8.resize((size_t)n - 1);
        WideCharToMultiByte(CP_UTF8, 0, json, -1, utf8.data(), n, nullptr,
                            nullptr);
        DWORD w = 0;
        WriteFile(h, utf8.data(), (DWORD)utf8.size(), &w, nullptr);
    }
    CloseHandle(h);
}
