// WebViewHost — window + WebView2 controller. Evergreen runtime; if the
// runtime is missing the window shows a plain error with the remediation
// steps (online bootstrapper / offline standalone installer) instead of a
// blank page. Dev URL override via LUNAR_INSTALLER_DEV_URL (vite dev).

#include "WebViewHost.h"

#include <shlobj.h>
#include <wrl.h>

#include <cstdarg>
#include <cstdio>

namespace {
// Permanent lightweight diagnostics (Temp\LunarBA_diag.log): traces the
// Burn -> BA -> WebView2 -> mapping -> navigation chain so a white window
// is diagnosable from the log alone. Tiny volume, no PII.
void DiagLog(const wchar_t* fmt, ...) {
    wchar_t path[MAX_PATH]{};
    DWORD n = GetTempPathW(MAX_PATH, path);
    if (n == 0 || n >= MAX_PATH)
        return;
    wcscat_s(path, L"LunarBA_diag.log");
    FILE* f = nullptr;
    _wfopen_s(&f, path, L"a, ccs=UTF-8");
    if (!f)
        return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    fwprintf_s(f, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    va_list ap;
    va_start(ap, fmt);
    vfwprintf_s(f, fmt, ap);
    va_end(ap);
    fwprintf_s(f, L"\n");
    fclose(f);
}

bool PathExistsDir(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
} // namespace

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {
constexpr wchar_t kWndClass[] = L"LunarInstallerWebView";
constexpr wchar_t kAppData[] = L"Lunar Player\\InstallerWebView";

bool ReadEnvUrl(std::wstring& out) {
    wchar_t buf[2084]{};
    DWORD n = GetEnvironmentVariableW(L"LUNAR_INSTALLER_DEV_URL", buf,
                                      (DWORD)(sizeof(buf) / sizeof(buf[0])));
    if (n == 0 || n >= (DWORD)(sizeof(buf) / sizeof(buf[0])))
        return false;
    out.assign(buf);
    return true;
}

bool UserDataDir(std::wstring& out) {
    wchar_t* base = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr,
                                    &base)))
        return false;
    out.assign(base);
    out += L"\\";
    out += kAppData;
    CoTaskMemFree(base);
    CreateDirectoryW(out.c_str(), nullptr);
    return true;
}

} // namespace

std::wstring WebViewHost::UiUrl() const {
    // Served through a virtual host mapping (SetVirtualHostNameToFolderMapping
    // in InitWebView): ES modules are blocked on file:// in Chromium, so the
    // production UI is NEVER loaded from a file URL.
    return L"https://appassets/index.html" + m_query;
}

namespace {
std::wstring UiDistDir() {
    // Dev override for testing the virtual-host path without Burn.
    wchar_t env[32768]{};
    DWORD n = GetEnvironmentVariableW(L"LUNAR_UI_DIR", env,
                                      (DWORD)(sizeof(env) / sizeof(env[0])));
    if (n > 0 && n < (DWORD)(sizeof(env) / sizeof(env[0])))
        return env;
    // Production: ui\dist sits NEXT TO THIS DLL (BA payload Name preserves
    // structure). GetModuleFileNameW(NULL) would return the Burn engine
    // host exe in the clean room — WRONG directory. Use our own module.
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                           | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&UiDistDir), &self);
    wchar_t mod[MAX_PATH]{};
    GetModuleFileNameW(self ? self : nullptr, mod, MAX_PATH);
    std::wstring dir(mod);
    const size_t slash = dir.find_last_of(L"\\/");
    std::wstring base =
        (slash == std::wstring::npos ? L"." : dir.substr(0, slash));
    return base + L"\\ui\\dist";
}
} // namespace

WebViewHost::WebViewHost(HINSTANCE hInstance, IRpcBackend* backend)
    : m_hInstance(hInstance)
    , m_hwnd(nullptr)
    , m_backend(backend)
    , m_controller(nullptr)
    , m_web(nullptr) {
}

WebViewHost::~WebViewHost() {
    if (m_web) {
        m_web->Release();
        m_web = nullptr;
    }
    if (m_controller) {
        m_controller->Close();
        m_controller->Release();
        m_controller = nullptr;
    }
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

LRESULT CALLBACK WebViewHost::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    WebViewHost* self = nullptr;
    if (m == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        self = reinterpret_cast<WebViewHost*>(cs->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<WebViewHost*>(
            GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    switch (m) {
    case WM_SIZE:
        if (self && self->m_controller) {
            RECT r{};
            GetClientRect(h, &r);
            self->m_controller->put_Bounds(r);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

bool WebViewHost::Create(const std::wstring& title, int width, int height) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = WebViewHost::WndProc;
    wc.hInstance = m_hInstance;
    wc.lpszClassName = kWndClass;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    const DWORD style = WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME;
    RECT rc{ 0, 0, width, height };
    AdjustWindowRect(&rc, style, FALSE);
    const int sw = GetSystemMetrics(SM_CXSCREEN);
    const int sh = GetSystemMetrics(SM_CYSCREEN);
    m_hwnd = CreateWindowExW(0, kWndClass, title.c_str(), style,
                             (sw - (rc.right - rc.left)) / 2,
                             (sh - (rc.bottom - rc.top)) / 2,
                             rc.right - rc.left, rc.bottom - rc.top,
                             nullptr, nullptr, m_hInstance, this);
    if (!m_hwnd)
        return false;
    InitWebView();
    return true;
}

void WebViewHost::Show(int cmdShow) {
    if (m_hwnd)
        ShowWindow(m_hwnd, cmdShow);
}

void WebViewHost::PostEvent(const std::wstring& json) {
    if (m_web)
        m_web->PostWebMessageAsJson(json.c_str());
}

namespace {
// Minimal JSON string escaper for bridge replies.
std::wstring JsonEscape(const std::wstring& s) {
    std::wstring o;
    for (wchar_t c : s) {
        switch (c) {
        case L'"': o += L"\\\""; break;
        case L'\\': o += L"\\\\"; break;
        case L'\n': o += L"\\n"; break;
        case L'\r': o += L"\\r"; break;
        case L'\t': o += L"\\t"; break;
        default:
            if (c < 0x20) {
                wchar_t b[8]{};
                swprintf_s(b, L"\\u%04x", c);
                o += b;
            } else {
                o += c;
            }
        }
    }
    return o;
}

// Tiny RPC parser: {"id":N,"method":"M","args":{...}} (flat scan, no dep).
bool ParseRpc(const std::wstring& json, long& id, std::wstring& method,
              std::wstring& argsJson) {
    const wchar_t* p = json.c_str();
    const wchar_t* pi = wcsstr(p, L"\"id\"");
    if (!pi)
        return false;
    id = _wtol(wcschr(pi, L':') + 1);
    const wchar_t* pm = wcsstr(p, L"\"method\"");
    if (!pm)
        return false;
    const wchar_t* colon = wcschr(pm, L':');
    const wchar_t* v1 = wcschr(colon, L'"');
    if (!v1)
        return false;
    const wchar_t* v2 = wcschr(v1 + 1, L'"');
    if (!v2)
        return false;
    method.assign(v1 + 1, v2);
    const wchar_t* pa = wcsstr(p, L"\"args\"");
    if (pa) {
        const wchar_t* b = wcschr(pa, L'{');
        if (b) {
            int depth = 0;
            const wchar_t* e = b;
            for (; *e; ++e) {
                if (*e == L'{')
                    ++depth;
                else if (*e == L'}') {
                    --depth;
                    if (depth == 0) {
                        ++e;
                        break;
                    }
                }
            }
            argsJson.assign(b, e);
        }
    }
    if (argsJson.empty())
        argsJson = L"{}";
    return true;
}
} // namespace

void WebViewHost::OnWebMessage(const std::wstring& json) {
    DiagLog(L"WebMessage: %s", json.substr(0, 120).c_str());
    if (!m_backend || !m_web)
        return;
    long id = 0;
    std::wstring method, args;
    std::wstring reply;
    if (!ParseRpc(json, id, method, args)) {
        reply = L"{\"id\":0,\"error\":\"malformed RPC\"}";
    } else {
        try {
            std::wstring result = m_backend->Dispatch(method, args);
            reply = L"{\"id\":" + std::to_wstring(id) + L",\"result\":"
                + result + L"}";
        } catch (const std::wstring& err) {
            reply = L"{\"id\":" + std::to_wstring(id) + L",\"error\":\""
                + JsonEscape(err) + L"\"}";
        } catch (...) {
            reply = L"{\"id\":" + std::to_wstring(id)
                + L",\"error\":\"internal bridge failure\"}";
        }
    }
    m_web->PostWebMessageAsJson(reply.c_str());
}

void WebViewHost::InitWebView() {
    std::wstring userData;
    if (!UserDataDir(userData)) {
        DiagLog(L"InitWebView: user-data dir unavailable");
        return;
    }
    std::wstring devUrl;
    const bool dev = ReadEnvUrl(devUrl);
    WebViewHost* self = this;
    DiagLog(L"InitWebView: dev=%d userData=%s", dev ? 1 : 0,
            userData.c_str());
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userData.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [self, dev,
             devUrl](HRESULT r,
                     ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(r) || !env) {                    // Runtime missing: show remediation instead of blank.
                    DiagLog(L"environment FAILED hr=0x%08x", (unsigned)r);
                    MessageBoxW(
                        self->m_hwnd,
                        L"WebView2 Runtime was not found.\n\n"
                        L"Install it online with the Evergreen Bootstrapper, "
                        L"or use the Evergreen Standalone Installer for "
                        L"offline machines, then restart this installer.",
                        L"Lunar Player Installer", MB_ICONERROR | MB_OK);
                    return S_OK;
                }
                // NOTE: no virtual-host mapping here. The mapping API
                // (SetVirtualHostNameToFolderMapping) lives on
                // ICoreWebView2_3, i.e. the CoreWebView2 CONTROL object —
                // never on the environment. Querying it on `env` fails
                // with E_NOINTERFACE and yields a permanently white
                // window. The mapping is registered below, on m_web,
                // before Navigate.
                if (!dev) {
                    const std::wstring dist = UiDistDir();
                    DiagLog(L"ui dist -> %s (exists=%d)", dist.c_str(),
                            PathExistsDir(dist) ? 1 : 0);
                    // Browser version for diagnosability.
                    LPWSTR bv = nullptr;
                    if (SUCCEEDED(
                            GetAvailableCoreWebView2BrowserVersionString(
                                nullptr, &bv))) {
                        DiagLog(L"WebView2 browser version=%s",
                                bv ? bv : L"?");
                        CoTaskMemFree(bv);
                    } else {
                        DiagLog(L"WebView2 browser version query failed");
                    }
                }
                env->CreateCoreWebView2Controller(
                    self->m_hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [self, dev,
                         devUrl](HRESULT r2,
                                 ICoreWebView2Controller* c) -> HRESULT {
                            if (FAILED(r2) || !c) {
                                DiagLog(L"controller FAILED hr=0x%08x",
                                        (unsigned)r2);
                                return S_OK;
                            }
                            // Ownership of c transfers to us (COM out-param).
                            self->m_controller = c;
                            RECT bounds{};
                            GetClientRect(self->m_hwnd, &bounds);
                            self->m_controller->put_Bounds(bounds);
                            self->m_controller->get_CoreWebView2(
                                &self->m_web);
                            if (!self->m_web)
                                return S_OK;
                            // Production UI serving: virtual-host mapping
                            // so ES modules load (file:// blocks them in
                            // Chromium). The mapping is per-WebView
                            // (ICoreWebView2_3 on the control) and must
                            // be registered BEFORE Navigate. Dev servers
                            // (LUNAR_INSTALLER_DEV_URL) skip it.
                            if (!dev) {
                                const std::wstring dist = UiDistDir();
                                Microsoft::WRL::ComPtr<ICoreWebView2>
                                    webSp(self->m_web);
                                Microsoft::WRL::ComPtr<ICoreWebView2_3>
                                    web3;
                                // ComPtr::As performs the QI with correct
                                // ref ownership (never QI through
                                // IID_PPV_ARGS on a ComPtr).
                                const HRESULT qhr = webSp.As(&web3);
                                DiagLog(L"QI ICoreWebView2_3 hr=0x%08x",
                                        (unsigned)qhr);
                                if (SUCCEEDED(qhr) && web3) {
                                    const HRESULT mhr =
                                        web3->SetVirtualHostNameToFolderMapping(
                                            L"appassets", dist.c_str(),
                                            COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
                                    DiagLog(L"mapping result hr=0x%08x",
                                            (unsigned)mhr);
                                    if (FAILED(mhr)) {
                                        MessageBoxW(
                                            self->m_hwnd,
                                            L"The installer UI could not start (resource mapping failed).\n\n"
                                            L"Close other installer windows and run LunarPlayerInstaller.exe again.",
                                            L"Lunar Player Installer",
                                            MB_ICONERROR | MB_OK);
                                    }
                                } else {
                                    // Without the mapping,
                                    // https://appassets/ can never load:
                                    // fail loudly, never white.
                                    DiagLog(
                                        L"ICoreWebView2_3 unavailable, aborting UI");
                                    MessageBoxW(
                                        self->m_hwnd,
                                        L"The installer UI could not start (WebView2 too old or unavailable).\n\n"
                                        L"Install the Evergreen WebView2 Runtime, then restart this installer.",
                                        L"Lunar Player Installer",
                                        MB_ICONERROR | MB_OK);
                                }
                            }
                            ICoreWebView2Settings* settings = nullptr;
                            if (SUCCEEDED(self->m_web->get_Settings(
                                    &settings))) {
                                settings->put_AreDefaultContextMenusEnabled(
                                    FALSE);
#ifdef NDEBUG
                                settings->put_AreDevToolsEnabled(FALSE);
#else
                                settings->put_AreDevToolsEnabled(TRUE);
#endif
                                settings->Release();
                            }
                            self->m_web->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [self](
                                        ICoreWebView2*,
                                        ICoreWebView2WebMessageReceivedEventArgs*
                                            args) -> HRESULT {
                                        LPWSTR json = nullptr;
                                        if (SUCCEEDED(
                                                args->TryGetWebMessageAsString(
                                                    &json))) {
                                            self->OnWebMessage(json ? json
                                                                    : L"{}");
                                            CoTaskMemFree(json);
                                        }
                                        return S_OK;
                                    })
                                    .Get(),
                                nullptr);
                            const std::wstring url =
                                dev ? devUrl : self->UiUrl();
                            DiagLog(L"Navigate %s", url.c_str());
                            const HRESULT nhr =
                                self->m_web->Navigate(url.c_str());
                            DiagLog(L"Navigate hr=0x%08x", (unsigned)nhr);
                            // Navigation result (success/error status).
                            EventRegistrationToken navToken{};
                            const HRESULT ch =
                                self->m_web->add_NavigationCompleted(
                                    Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                        [self](
                                            ICoreWebView2*,
                                            ICoreWebView2NavigationCompletedEventArgs*
                                                a) -> HRESULT {
                                            BOOL ok = FALSE;
                                            COREWEBVIEW2_WEB_ERROR_STATUS
                                            st =
                                                COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                                            if (a) {
                                                a->get_IsSuccess(&ok);
                                                a->get_WebErrorStatus(&st);
                                            }
                                            DiagLog(
                                                L"NavigationCompleted ok=%d status=%d",
                                                ok ? 1 : 0, (int)st);
                                            return S_OK;
                                        })
                                        .Get(),
                                    &navToken);
                            DiagLog(L"add_NavigationCompleted hr=0x%08x",
                                    (unsigned)ch);
                            return S_OK;
                        })
                        .Get());
                return S_OK;
            })
            .Get());
    (void)hr;
}
