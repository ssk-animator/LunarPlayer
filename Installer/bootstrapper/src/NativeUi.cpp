// NativeInstallerWindow — borderless Win32 + Direct2D/DirectWrite UI.
// Custom title bar + fixed footer + scrollable content viewport.
// Dark Lunar theme matching Installtion UI_Ref.png.

#include "NativeUi.h"

#include "InstallerBridge.h"

#include <shellapi.h>
#include <stdio.h>
#include <windowsx.h>
#include <wincodec.h>

namespace {
constexpr float kTitleH = 52.0f;
constexpr float kFootH = 100.0f;
constexpr float kContentMax = 880.0f;
constexpr float kMarginMin = 24.0f;
constexpr UINT kTimer = 7;
constexpr UINT WM_TRAY = WM_APP + 60;
constexpr UINT kTrayShow = 1001;
constexpr UINT kTrayExit = 1002;

D2D1_COLOR_F C(unsigned rgb, float alpha255 = 255.0f) {
    return D2D1::ColorF(((rgb >> 16) & 0xFF) / 255.0f,
                        ((rgb >> 8) & 0xFF) / 255.0f,
                        (rgb & 0xFF) / 255.0f, alpha255 / 255.0f);
}

unsigned Hash2(int x, int y) {
    unsigned h = (unsigned)(x * 374761393 + y * 668265263);
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// Vertically centered text top inside a control of height h using a
// font of (scaled) pixel size fontPx (line ≈ 1.25x).
float VCenter(float top, float h, float fontPx) {
    return top + (h - fontPx * 1.25f) / 2.0f;
}

D2D1_RECT_F R(float l, float t, float r, float b) {
    D2D1_RECT_F rc{};
    rc.left = l;
    rc.top = t;
    rc.right = r;
    rc.bottom = b;
    return rc;
}

bool InRect(const D2D1_RECT_F& rc, int x, int y) {
    return x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom;
}
} // namespace

NativeInstallerWindow::NativeInstallerWindow(HINSTANCE hInstance,
                                             InstallerBridge* bridge,
                                             UiActions actions)
    : m_hInstance(hInstance)
    , m_hwnd(nullptr)
    , m_bridge(bridge)
    , m_actions(std::move(actions)) {
}

NativeInstallerWindow::~NativeInstallerWindow() {
    DiscardDevice();
    TrayRemove();
    if (m_hWndIcon) {
        DestroyIcon(m_hWndIcon);
        m_hWndIcon = nullptr;
    }
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

bool NativeInstallerWindow::Create(const std::wstring& title, int width,
                                   int height) {
    const wchar_t* cls = L"LunarInstallerNative";
    WNDCLASSW wc{};
    wc.lpfnWndProc = NativeInstallerWindow::WndProc;
    wc.hInstance = m_hInstance;
    wc.lpszClassName = cls;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    RegisterClassW(&wc);

    // Borderless but resizable: no caption, keep thick frame + system
    // boxes so resize grips, minimize/maximize animations and the
    // system menu keep working; the title bar is fully owner-drawn.
    const DWORD style =
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX
        | WS_SYSMENU | WS_CLIPCHILDREN;
    // Explorer-restart recovery for the tray icon (re-add on recreate).
    m_taskbarMsg = RegisterWindowMessageW(L"TaskbarCreated");
    RECT rc{ 0, 0, width, height };
    AdjustWindowRectEx(&rc, style, FALSE, 0);
    const int sw = GetSystemMetrics(SM_CXSCREEN);
    const int sh = GetSystemMetrics(SM_CYSCREEN);
    m_hwnd = CreateWindowExW(WS_EX_APPWINDOW, cls, title.c_str(), style,
                             (sw - (rc.right - rc.left)) / 2,
                             (sh - (rc.bottom - rc.top)) / 2,
                             rc.right - rc.left, rc.bottom - rc.top,
                             nullptr, nullptr, m_hInstance, this);
    if (!m_hwnd)
        return false;
    m_scale = GetDpiForWindow(m_hwnd) / 96.0f;
    if (m_scale <= 0.0f)
        m_scale = 1.0f;
    // Real window/taskbar icon from the packaged logo (same asset as
    // the tray icon), so the installer never shows a generic icon.
    EnsureWindowIcon();
    if (m_bridge)
        m_launchAfter = m_bridge->IsLaunchAfter();
    RefreshData();
    return true;
}

void NativeInstallerWindow::Show(int cmdShow) {
    if (m_hwnd)
        ShowWindow(m_hwnd, cmdShow);
}

void NativeInstallerWindow::RefreshData() {
    if (!m_bridge)
        return;
    m_components.clear();
    for (const auto& c : m_bridge->Components()) {
        NativeComponentView v;
        v.id = c.id;
        v.name = c.name;
        v.description = c.description;
        v.required = c.required;
        v.selected = c.selected;
        v.sizeLabel = (c.id == L"aiModels" && c.sizeBytes == 0)
            ? L"--"
            : FormatBytes(c.sizeBytes);
        v.dlLabel = (c.downloadBytes > 0)
            ? (FormatDownload(c.downloadBytes) + L" \u2193 download")
            : L"";
        m_components.push_back(v);
    }
    m_installPath = m_bridge->InstallPath();
    m_version = m_bridge->BundleVersion();
    m_requiredBytes = m_bridge->SelectedBytes();
    m_availableBytes = m_bridge->AvailableBytes();
    m_launchAfter = m_bridge->IsLaunchAfter();
    m_logoPath = m_bridge->LogoPath();
    if (m_hwnd) {
        RECT r{};
        GetClientRect(m_hwnd, &r);
        D2D1_SIZE_F sz{ (float)(r.right - r.left),
                        (float)(r.bottom - r.top) };
        Layout(sz);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void NativeInstallerWindow::SetProgress(const std::wstring& phase,
                                        const std::wstring& detail,
                                        DWORD percent) {
    m_screen = NativeScreen::Installing; m_focusIdx = -1;
    m_phase = phase;
    m_detail = detail;
    m_percent = percent > 100 ? 100 : percent;
    if (m_hwnd)
        InvalidateRect(m_hwnd, nullptr, FALSE);
}

void NativeInstallerWindow::SetComplete() {
    m_completeTitle = L"Lunar Player is ready";
    m_completeDetail = L"Installation completed successfully.";
    m_completeShowVersion = true;
    m_completeUninstalled = false;
    m_screen = NativeScreen::Complete; m_focusIdx = -1;
    if (m_hwnd)
        InvalidateRect(m_hwnd, nullptr, FALSE);
}

void NativeInstallerWindow::SetCompleteEx(const std::wstring& title,
                                          const std::wstring& detail) {
    m_completeTitle = title.empty() ? L"Done" : title;
    m_completeDetail = detail;
    m_completeShowVersion = false;
    m_completeUninstalled = false;
    m_screen = NativeScreen::Complete; m_focusIdx = -1;
    if (m_hwnd)
        InvalidateRect(m_hwnd, nullptr, FALSE);
}

void NativeInstallerWindow::SetUninstalled() {
    m_completeTitle = L"Lunar Player was removed";
    m_completeDetail =
        L"Shortcuts, files and registration were removed.";
    m_completeShowVersion = false;
    m_completeUninstalled = true;
    m_screen = NativeScreen::Complete; m_focusIdx = -1;
    if (m_hwnd) {
        RECT r{};
        GetClientRect(m_hwnd, &r);
        D2D1_SIZE_F sz{ (float)(r.right - r.left),
                        (float)(r.bottom - r.top) };
        Layout(sz);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void NativeInstallerWindow::SetMaintenance(const std::wstring& version,
                                           const std::wstring& path) {
    m_installedVersion = version;
    m_installedPath = path;
    m_screen = NativeScreen::Maintenance; m_focusIdx = -1;
    if (m_hwnd) {
        RECT r{};
        GetClientRect(m_hwnd, &r);
        D2D1_SIZE_F sz{ (float)(r.right - r.left),
                        (float)(r.bottom - r.top) };
        Layout(sz);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void NativeInstallerWindow::SetError(const std::wstring& message,
                                     bool cancelled) {
    m_screen = NativeScreen::Error; m_focusIdx = -1;
    m_error = message;
    m_cancelled = cancelled;
    if (m_hwnd)
        InvalidateRect(m_hwnd, nullptr, FALSE);
}

void NativeInstallerWindow::SetTestScreen(NativeScreen s) {
    m_screen = s; m_focusIdx = -1;
    if (m_hwnd)
        InvalidateRect(m_hwnd, nullptr, FALSE);
}

std::wstring NativeInstallerWindow::FormatBytes(uint64_t bytes) {    wchar_t b[64]{};
    if (bytes == 0) {
        wcscpy_s(b, L"--");
    } else if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        swprintf_s(b, L"%.2f GB",
                   bytes / (1024.0 * 1024.0 * 1024.0));
    } else {
        swprintf_s(b, L"%llu MB", bytes / (1024ULL * 1024ULL));
    }
    return b;
}

// Download sizes: one decimal (e.g. 6.0/39.4/430.5 MB) so the small
// attributed slices don't truncate to misleading integers.
std::wstring NativeInstallerWindow::FormatDownload(uint64_t bytes) {
    wchar_t b[64]{};
    if (bytes == 0) {
        wcscpy_s(b, L"--");
    } else if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        swprintf_s(b, L"%.2f GB",
                   bytes / (1024.0 * 1024.0 * 1024.0));
    } else {
        swprintf_s(b, L"%.1f MB", bytes / (1024.0 * 1024.0));
    }
    return b;
}

// ---- Layout: title/footer rects (screen coords) + content height.
// Content rows are drawn by ContentWalk, which records hits in
// CONTENT coords (paint subtracts the scroll offset). This single walk
// guarantees hits and pixels always agree.
void NativeInstallerWindow::Layout(D2D1_SIZE_F size) {
    m_hits.clear();
    // Reference-anchored layout: fixed left margin, particles keep the
    // right side. Only narrow windows fall back to a small margin.
    const float x0 = (size.width <= S(928)) ? S(24) : S(64);
    float contentW = size.width - x0 - S(24);
    if (contentW > S(kContentMax))
        contentW = S(kContentMax);
    if (contentW < S(200))
        contentW = S(200);
    m_layoutX = x0;
    m_layoutW = contentW;
    // Title-bar rects (screen coords).
    const float th = S(kTitleH);
    m_capClose = R(size.width - S(46), 0, size.width, th);
    m_capMax = R(size.width - S(92), 0, size.width - S(46), th);
    m_capMin = R(size.width - S(138), 0, size.width - S(92), th);
    m_capDrag = R(0, 0, size.width - S(138), th);
    // Footer rects per screen (screen coords).
    const float fy = size.height - S(100);
    auto fhit = [&](const std::wstring& id, D2D1_RECT_F rc) {
        Hit h;
        h.id = id;
        h.rc = rc;
        h.inFooter = true;
        m_hits.push_back(h);
    };
    if (m_screen == NativeScreen::Install) {
        m_footCancel = R(x0 + contentW - S(300), fy + S(26),
                         x0 + contentW - S(172), fy + S(74));
        m_footInstall = R(x0 + contentW - S(160), fy + S(26),
                          x0 + contentW, fy + S(74));
        fhit(L"cancel", m_footCancel);
        fhit(L"install", m_footInstall);
    } else if (m_screen == NativeScreen::Installing) {
        m_footCancel = R(x0, fy + S(26), x0 + S(140), fy + S(74));
        m_footInstall = R(0, 0, 0, 0);
        fhit(L"cancel", m_footCancel);
    } else if (m_screen == NativeScreen::Complete) {
        m_footCancel = R(x0 + contentW - S(300), fy + S(26),
                         x0 + contentW - S(172), fy + S(74));
        m_footInstall = R(x0 + contentW - S(160), fy + S(26),
                          x0 + contentW, fy + S(74));
        m_footMid = R(0, 0, 0, 0);
        fhit(L"close2", m_footCancel);
        // Uninstall completion: Close only (the product is gone, so no
        // Launch target exists).
        if (!m_completeUninstalled)
            fhit(L"launch", m_footInstall);
    } else if (m_screen == NativeScreen::Maintenance) {
        m_footCancel = R(x0, fy + S(26), x0 + S(140), fy + S(74));
        m_footMid = R(x0 + contentW - S(308), fy + S(26),
                      x0 + contentW - S(164), fy + S(74));
        m_footInstall = R(x0 + contentW - S(152), fy + S(26),
                          x0 + contentW, fy + S(74));
        fhit(L"mclose", m_footCancel);
        fhit(L"repair", m_footMid);
        fhit(L"uninstall", m_footInstall);
    } else { // Error
        m_footCancel = R(x0, fy + S(26), x0 + S(140), fy + S(74));
        m_footInstall = R(0, 0, 0, 0);
        fhit(L"back", m_footCancel);
    }
    m_contentH = ContentWalk(nullptr, x0, contentW, 0.0f);
    // Keyboard focus order: content top-down, then footer actions.
    m_focusOrder.clear();
    for (auto& h : m_hits) {
        if (!h.inFooter)
            m_focusOrder.push_back(h.id);
    }
    for (auto& h : m_hits) {
        if (h.inFooter)
            m_focusOrder.push_back(h.id);
    }
    if (m_focusIdx >= (int)m_focusOrder.size())
        m_focusIdx = -1;
    const float viewH = size.height - S(kTitleH) - S(100);
    const float maxS = (m_contentH > viewH) ? (m_contentH - viewH) : 0.0f;
    if (m_scrollY > maxS)
        m_scrollY = maxS;
    if (m_scrollY < 0.0f)
        m_scrollY = 0.0f;
    m_scrollMax = maxS;
}

LRESULT CALLBACK NativeInstallerWindow::WndProc(HWND h, UINT m, WPARAM w,
                                                LPARAM l) {
    NativeInstallerWindow* self = nullptr;
    if (m == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        self = reinterpret_cast<NativeInstallerWindow*>(
            cs->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<NativeInstallerWindow*>(
            GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    if (!self)
        return DefWindowProcW(h, m, w, l);
    // Explorer was restarted: our tray icon died with it — re-add.
    if (self->m_taskbarMsg != 0 && m == self->m_taskbarMsg) {
        self->m_trayAdded = false;
        self->m_trayVerified = false;
        self->m_trayRetries = 0;
        self->TrayAdd();
        return 0;
    }
    switch (m) {
    case WM_CREATE:
        self->EnsureDevice();
        self->TrayAdd();
        SetTimer(h, kTimer, 120, nullptr);
        return 0;
    case WM_SIZE:
        if (self->m_rt) {
            RECT r{};
            GetClientRect(h, &r);
            self->m_rt->Resize(
                D2D1::SizeU((UINT32)(r.right - r.left),
                            (UINT32)(r.bottom - r.top)));
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mi = reinterpret_cast<MINMAXINFO*>(l);
        mi->ptMinTrackSize.x = 980;
        mi->ptMinTrackSize.y = 680;
        return 0;
    }
    case WM_DPICHANGED: {
        self->m_scale = HIWORD(w) / 96.0f;
        if (self->m_scale <= 0.0f)
            self->m_scale = 1.0f;
        self->DiscardDevice();
        self->EnsureDevice();
        auto* rc = reinterpret_cast<RECT*>(l);
        SetWindowPos(h, nullptr, rc->left, rc->top,
                     rc->right - rc->left, rc->bottom - rc->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_NCHITTEST: {
        LRESULT r = DefWindowProcW(h, m, w, l);
        if (r == HTCLIENT) {
            const int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
            RECT wr{};
            GetWindowRect(h, &wr);
            const int lx = x - wr.left, ly = y - wr.top;
            if (InRect(self->m_capDrag, lx, ly))
                return HTCAPTION;
        }
        return r;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        self->OnPaint();
        return 0;
    case WM_TIMER:
        if (self->m_demo
            && self->m_screen == NativeScreen::Installing) {
            DWORD next = self->m_percent + 2;
            if (next >= 100) {
                self->m_demo = false;
                self->SetComplete();
            } else {
                self->SetProgress(L"Installing (demo)",
                                  L"LunarPlayer.msi", next);
            }
        }
        // Tray self-heal: retry a failed NIM_ADD a few times, then
        // one-shot verify the icon is really registered (GetRect).
        if (!self->m_trayAdded && self->m_trayRetries < 5) {
            ++self->m_trayRetries;
            self->TrayAdd();
        } else if (self->m_trayAdded && !self->m_trayVerified) {
            self->TrayVerify();
        }
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE:
        self->OnMove(GET_X_LPARAM(l), GET_Y_LPARAM(l));
        return 0;
    case WM_LBUTTONDOWN:
        self->m_pressed = true;
        SetCapture(h);
        self->OnMove(GET_X_LPARAM(l), GET_Y_LPARAM(l));
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP:
        if (self->m_pressed) {
            self->m_pressed = false;
            ReleaseCapture();
            self->OnClick(GET_X_LPARAM(l), GET_Y_LPARAM(l));
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEWHEEL:
        self->OnWheel(GET_WHEEL_DELTA_WPARAM(w));
        return 0;
    case WM_KEYDOWN:
        self->OnKey((int)w, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT && !self->m_hover.empty()) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        self->TrayRemove();
        KillTimer(h, kTimer);
        PostQuitMessage(0);
        return 0;
    case WM_TRAY:
        if (l == WM_LBUTTONUP) {
            if (IsIconic(h))
                ShowWindow(h, SW_RESTORE);
            SetForegroundWindow(h);
        } else if (l == WM_RBUTTONUP) {
            POINT pt{};
            GetCursorPos(&pt);
            self->TrayMenu(pt.x, pt.y);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(w) == kTrayShow) {
            if (IsIconic(h))
                ShowWindow(h, SW_RESTORE);
            SetForegroundWindow(h);
            return 0;
        }
        if (LOWORD(w) == kTrayExit) {
            if (self->m_actions.onExitTray)
                self->m_actions.onExitTray();
            else
                DestroyWindow(h);
            return 0;
        }
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

void NativeInstallerWindow::OnWheel(int delta) {
    m_scrollY -= (delta / WHEEL_DELTA) * S(60);
    RECT r{};
    GetClientRect(m_hwnd, &r);
    D2D1_SIZE_F sz{ (float)(r.right - r.left),
                    (float)(r.bottom - r.top) };
    Layout(sz);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void NativeInstallerWindow::OnKey(int vk, bool shift) {
    if (vk == VK_TAB) {
        if (m_focusOrder.empty())
            return;
        if (m_focusIdx < 0) {
            m_focusIdx = shift ? (int)m_focusOrder.size() - 1 : 0;
        } else if (shift) {
            m_focusIdx =
                (m_focusIdx - 1 + (int)m_focusOrder.size())
                % (int)m_focusOrder.size();
        } else {
            m_focusIdx = (m_focusIdx + 1) % (int)m_focusOrder.size();
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
    } else if (vk == VK_SPACE || vk == VK_RETURN) {
        if (m_focusIdx >= 0 && m_focusIdx < (int)m_focusOrder.size())
            ActivateHit(m_focusOrder[(size_t)m_focusIdx]);
    } else if (vk == VK_UP) {
        m_scrollY -= S(60);
        RECT r{};
        GetClientRect(m_hwnd, &r);
        D2D1_SIZE_F sz{ (float)(r.right - r.left),
                        (float)(r.bottom - r.top) };
        Layout(sz);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    } else if (vk == VK_DOWN) {
        m_scrollY += S(60);
        RECT r{};
        GetClientRect(m_hwnd, &r);
        D2D1_SIZE_F sz{ (float)(r.right - r.left),
                        (float)(r.bottom - r.top) };
        Layout(sz);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    } else if (vk == VK_ESCAPE) {
        ActivateHit(L"cancel");
    }
}

void NativeInstallerWindow::OnMove(int x, int y) {
    std::wstring hov;
    if (InRect(m_capMin, x, y)) hov = L"min";
    else if (InRect(m_capMax, x, y)) hov = L"max";
    else if (InRect(m_capClose, x, y)) hov = L"close";
    else {
        RECT r{};
        GetClientRect(m_hwnd, &r);
        const float fy = (float)(r.bottom - r.top) - S(100);
        for (const auto& h : m_hits) {
            if (h.inFooter) {
                if (InRect(h.rc, x, y)) {
                    hov = h.id;
                    break;
                }
            } else {
                const float yy = y - S(kTitleH) + m_scrollY;
                if (x >= h.rc.left && x < h.rc.right && yy >= h.rc.top
                    && yy < h.rc.bottom) {
                    hov = h.id;
                    break;
                }
            }
        }
        (void)fy;
    }
    if (hov != m_hover) {
        m_hover = hov;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}
void NativeInstallerWindow::OnClick(int x, int y) {
    if (InRect(m_capMin, x, y)) {
        ShowWindow(m_hwnd, SW_MINIMIZE);
        return;
    }
    if (InRect(m_capMax, x, y)) {
        ShowWindow(m_hwnd,
                   IsZoomed(m_hwnd) ? SW_RESTORE : SW_MAXIMIZE);
        return;
    }
    if (InRect(m_capClose, x, y)) {
        DestroyWindow(m_hwnd);
        return;
    }
    if (m_hasScroll && InRect(m_scrollTrack, x, y)) {
        RECT r{};
        GetClientRect(m_hwnd, &r);
        const float viewH =
            (float)(r.bottom - r.top) - S(kTitleH) - S(100);
        const float ratio =
            (y - m_scrollTrack.top)
            / (m_scrollTrack.bottom - m_scrollTrack.top);
        m_scrollY = ratio * m_contentH - viewH / 2.0f;
        D2D1_SIZE_F sz{ (float)(r.right - r.left),
                        (float)(r.bottom - r.top) };
        Layout(sz);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    for (const auto& h : m_hits) {
        bool inside = false;
        if (h.inFooter) {
            inside = InRect(h.rc, x, y);
        } else {
            const float yy = y - S(kTitleH) + m_scrollY;
            inside = (x >= h.rc.left && x < h.rc.right
                      && yy >= h.rc.top && yy < h.rc.bottom);
        }
        if (!inside)
            continue;
        ActivateHit(h.id);
        return;
    }
}

void NativeInstallerWindow::TestAction(const std::wstring& id) {
    if (id == L"tab") {
        OnKey(VK_TAB, false);
        return;
    }
    if (id == L"enter") {
        OnKey(VK_SPACE, false);
        return;
    }
    ActivateHit(id);
}

void NativeInstallerWindow::ActivateHit(const std::wstring& id) {
    if (id == L"scrolldown" || id == L"scrollup") {
        m_scrollY += (id == L"scrolldown" ? S(160) : -S(160));
        RECT r{};
        GetClientRect(m_hwnd, &r);
        D2D1_SIZE_F sz{ (float)(r.right - r.left),
                        (float)(r.bottom - r.top) };
        Layout(sz);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (id == L"browse" || id == L"path") {
            if (m_bridge && m_bridge->BrowseForFolder(m_hwnd))
                RefreshData();
            return;
        }
        if (id.compare(0, 7, L"toggle:") == 0) {
            std::wstring cid = id.substr(7);
            if (cid == L"launch") {
                m_launchAfter = !m_launchAfter;
                if (m_bridge)
                    m_bridge->SetLaunchAfter(m_launchAfter);
                InvalidateRect(m_hwnd, nullptr, FALSE);
            } else if (m_bridge) {
                m_bridge->SetOptionalSelected(cid,
                                              !m_bridge->IsSelected(cid));
                RefreshData();
            }
            return;
        }
        if (id == L"advanced") {
            m_showAdvanced = !m_showAdvanced;
            RECT r{};
            GetClientRect(m_hwnd, &r);
            D2D1_SIZE_F sz{ (float)(r.right - r.left),
                            (float)(r.bottom - r.top) };
            Layout(sz);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        if (id == L"install") {
            m_screen = NativeScreen::Installing; m_focusIdx = -1;
            m_phase = L"Starting";
            m_detail.clear();
            m_percent = 0;
            m_scrollY = 0.0f;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            if (m_actions.onInstall)
                m_actions.onInstall();
            return;
        }
        if (id == L"cancel") {
            if (m_screen == NativeScreen::Installing) {
                if (m_actions.onCancel)
                    m_actions.onCancel();
            } else {
                DestroyWindow(m_hwnd);
            }
            return;
        }
        if (id == L"back") {
            m_screen = NativeScreen::Install; m_focusIdx = -1;
            m_scrollY = 0.0f;
            RefreshData();
            if (m_actions.onBack)
                m_actions.onBack();
            return;
        }
        if (id == L"launch") {
            if (m_actions.onLaunch)
                m_actions.onLaunch();
            return;
        }
        if (id == L"close2" || id == L"mclose") {
            DestroyWindow(m_hwnd);
            return;
        }
        if (id == L"repair") {
            m_screen = NativeScreen::Installing; m_focusIdx = -1;
            m_phase = L"Starting repair";
            m_detail.clear();
            m_percent = 0;
            m_scrollY = 0.0f;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            if (m_actions.onRepair)
                m_actions.onRepair();
            return;
        }
        if (id == L"uninstall") {
            m_screen = NativeScreen::Installing; m_focusIdx = -1;
            m_phase = L"Starting uninstall";
            m_detail.clear();
            m_percent = 0;
            m_scrollY = 0.0f;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            if (m_actions.onUninstall)
                m_actions.onUninstall();
            return;
        }
        if (id == L"modify") {
            m_screen = NativeScreen::Install; m_focusIdx = -1;
            m_scrollY = 0.0f;
            RefreshData();
            if (m_actions.onModify)
                m_actions.onModify();
            return;
        }
}

// ---- Single content walk: draws at yBase when rt != null, always
// records content-coord hits, returns end-y (content coords).
float NativeInstallerWindow::ContentWalk(ID2D1RenderTarget* rt, float x0,
                                         float w, float yBase) {
    auto hit = [&](const std::wstring& id, D2D1_RECT_F rc) {
        Hit h;
        h.id = id;
        h.rc = rc;
        h.inFooter = false;
        m_hits.push_back(h);
    };
    auto T = [&](const std::wstring& s, float l, float t, float r,
                 float b, ID2D1Brush* br, IDWriteTextFormat* f) {
        if (rt) DrawText(rt, s, R(l, yBase + t, r, yBase + b), br, f);
    };
    float y = 0.0f;
    if (m_screen == NativeScreen::Install) {
        // Page title.
        if (rt) {
            DrawText(rt, L"Install ", R(x0, yBase + y, x0 + S(210),
                                        yBase + y + S(66)),
                     m_bText, m_fTitle);
            IDWriteTextLayout* lo = nullptr;
            m_dw->CreateTextLayout(L"Install ", 8, m_fTitle, 1000, 100,
                                   &lo);
            float adv = S(200);
            if (lo) {
                DWRITE_TEXT_METRICS tm{};
                if (SUCCEEDED(lo->GetMetrics(&tm)))
                    adv = tm.widthIncludingTrailingWhitespace;
                lo->Release();
            }
            DrawText(rt, L"Lunar Player",
                     R(x0 + adv, yBase + y, x0 + w, yBase + y + S(66)),
                     m_bAccent, m_fTitle);
        }
        y += S(66);
        T(L"A modern media player for creators, animators, VFX artists and editors.",
          x0, y, x0 + w, y + S(28), m_bDim, m_fSub);
        y += S(28);
        T(L"Installation Location", x0, y, x0 + w, y + S(26), m_bText,
          m_fHead);
        y += S(26);
        // Path box + Browse (vertically centered text).
        if (rt) {
            rt->FillRoundedRectangle(
                D2D1::RoundedRect(
                    R(x0, yBase + y, x0 + w - S(132), yBase + y + S(42)),
                    S(8), S(8)),
                m_bBox);
            rt->DrawRoundedRectangle(
                D2D1::RoundedRect(
                    R(x0, yBase + y, x0 + w - S(132), yBase + y + S(42)),
                    S(8), S(8)),
                (m_hover == L"path" || m_hover == L"browse") ? m_bAccentHi
                                                             : m_bBorder,
                1.0f);
            const bool bbHov = (m_hover == L"browse");
            rt->DrawRoundedRectangle(
                D2D1::RoundedRect(
                    R(x0 + w - S(120), yBase + y, x0 + w, yBase + y + S(42)),
                    S(8), S(8)),
                bbHov ? m_bAccentHi : m_bBorder, 1.5f);
        }
        // Vertically centered labels: box 42px, 15px font -> +12.
        T(m_installPath, x0 + S(14), y + S(12), x0 + w - S(146),
          y + S(42), m_bText, m_fBody);
        T(L"Browse\u2026", x0 + w - S(120), VCenter(y, S(42), S(15)),
          x0 + w, y + S(42), m_bText, m_fBtnC);
        hit(L"path", R(x0, y, x0 + w - S(132), y + S(42)));
        hit(L"browse", R(x0 + w - S(120), y, x0 + w, y + S(42)));
        y += S(42) + S(16);
        // Components card.
        const float cardTop = y;
        size_t core = 0, opt = 0;
        for (auto& c : m_components) {
            if (c.required) ++core;
            else ++opt;
        }
        float cardH = S(44) + core * S(62) + S(10) + S(44) + opt * S(62)
            + S(14);
        if (rt) {
            rt->FillRoundedRectangle(
                D2D1::RoundedRect(R(x0, yBase + cardTop, x0 + w,
                                            yBase + cardTop + cardH),
                                  S(12), S(12)),
                m_bCard);
            rt->DrawRoundedRectangle(
                D2D1::RoundedRect(R(x0, yBase + cardTop, x0 + w,
                                            yBase + cardTop + cardH),
                                  S(12), S(12)),
                m_bBorder, 1.0f);
        }
        auto row = [&](NativeComponentView& c, float ry) {
            const bool hov = (m_hover == (L"toggle:" + c.id));
            // cbp = paint coords (content + yBase); hits stay in
            // content coords.
            D2D1_RECT_F cb =
                R(x0 + S(18), yBase + ry + S(20), x0 + S(40),
                  yBase + ry + S(42));
            if (rt) {
                if (c.selected)
                    rt->FillRoundedRectangle(
                        D2D1::RoundedRect(cb, S(5), S(5)), m_bAccent);
                    rt->FillRoundedRectangle(
                        D2D1::RoundedRect(cb, S(5), S(5)), m_bAccent);
                rt->DrawRoundedRectangle(
                    D2D1::RoundedRect(cb, S(5), S(5)),
                    hov ? m_bAccentHi : m_bBorder, S(1.5));
                if (c.selected) {
                    ID2D1PathGeometry* g = nullptr;
                    if (SUCCEEDED(m_d2d->CreatePathGeometry(&g))) {
                        ID2D1GeometrySink* s = nullptr;
                        if (SUCCEEDED(g->Open(&s))) {
                            s->BeginFigure(
                                D2D1::Point2F(cb.left + S(5),
                                              cb.top + S(11)),
                                D2D1_FIGURE_BEGIN_HOLLOW);
                            s->AddLine(D2D1::Point2F(cb.left + S(9),
                                                     cb.top + S(15)));
                            s->AddLine(D2D1::Point2F(cb.left + S(17),
                                                     cb.top + S(6)));
                            s->EndFigure(D2D1_FIGURE_END_OPEN);
                            s->Close();
                            s->Release();
                        }
                        if (rt)
                            rt->DrawGeometry(g, m_bBg, S(2.5));
                        g->Release();
                    }
                }
            }
            std::wstring name = c.name;
            if (c.required)
                name += L" (required)";
            // Checkbox 22px; text block vertically centered in 62px row:
            // name 20px + desc 20px starting at +11.
            T(name, x0 + S(52), ry + S(9), x0 + w - S(230), ry + S(31),
              m_bText, m_fBody);
            T(c.description, x0 + S(52), ry + S(31), x0 + w - S(230),
              ry + S(53), m_bDim, m_fSmall);
            if (c.dlLabel.empty()) {
                T(c.sizeLabel, x0 + w - S(120), ry + S(21),
                  x0 + w - S(18), ry + S(43), m_bText, m_fBody);
            } else {
                // Two-line right column: download (accent) over
                // installed/on-disk (dim). Same row, no layout change.
                T(c.dlLabel, x0 + w - S(220), ry + S(8),
                  x0 + w - S(18), ry + S(30), m_bAccent, m_fBody);
                T(c.sizeLabel + L" on disk", x0 + w - S(220),
                  ry + S(30), x0 + w - S(18), ry + S(52), m_bDim,
                  m_fSmall);
            }            if (!c.required)
                hit(L"toggle:" + c.id,
                    R(x0 + S(8), ry, x0 + w - S(8), ry + S(62)));
        };
        auto secHead = [&](const wchar_t* title, const wchar_t* note,
                           float sy) {
            T(title, x0 + S(18), sy + S(12), x0 + S(300), sy + S(36),
              m_bText, m_fHead);
            if (rt)
                DrawText(rt, note,
                         R(x0 + w - S(320), yBase + sy + S(14), x0 + w - S(18),
                           yBase + sy + S(36)),
                         m_bDim, m_fSmall);
        };
        float ry = y + S(44);
        secHead(L"Core Components", L"Required for basic functionality",
                y);
        for (auto& c : m_components) {
            if (c.required) {
                row(c, ry);
                ry += S(62);
            }
        }
        const float divY = ry + S(10);
        if (rt)
            rt->DrawLine(
                D2D1::Point2F(x0 + S(18), yBase + divY),
                D2D1::Point2F(x0 + w - S(18), yBase + divY), m_bBorder,
                1.0f);
        ry += S(10);
        secHead(L"Optional Components", L"Enhance your experience", ry);
        ry += S(44);
        for (auto& c : m_components) {
            if (!c.required) {
                row(c, ry);
                ry += S(62);
            }
        }
        y = cardTop + cardH;
        y += S(14);
        // Advanced row.
        if (rt) {
            rt->DrawRoundedRectangle(
                D2D1::RoundedRect(R(x0, yBase + y, x0 + w, yBase + y + S(46)),
                                  S(8), S(8)),
                (m_hover == L"advanced") ? m_bAccentHi : m_bBorder,
                1.0f);
        }
        T(L"\u2699  Advanced options", x0 + S(18), y + S(13),
          x0 + w - S(40), y + S(38), m_bText, m_fBody);
        T(m_showAdvanced ? L"\u25BE" : L"\u25B8", x0 + w - S(44),
          y + S(13), x0 + w - S(18), y + S(38), m_bDim, m_fBody);
        hit(L"advanced", R(x0, y, x0 + w, y + S(46)));
        y += S(46);
        if (m_showAdvanced) {
            // Real option: launch after install.
            {
                const bool hov = (m_hover == L"toggle:launch");
                D2D1_RECT_F cb = R(x0 + S(18), yBase + y + S(12),
                                   x0 + S(40), yBase + y + S(34));
                if (rt) {
                    if (m_launchAfter)
                        rt->FillRoundedRectangle(
                            D2D1::RoundedRect(cb, S(5), S(5)), m_bAccent);
                    rt->DrawRoundedRectangle(
                        D2D1::RoundedRect(cb, S(5), S(5)),
                        hov ? m_bAccentHi : m_bBorder, S(1.5));
                    if (m_launchAfter) {
                        ID2D1PathGeometry* g = nullptr;
                        if (SUCCEEDED(m_d2d->CreatePathGeometry(&g))) {
                            ID2D1GeometrySink* s = nullptr;
                            if (SUCCEEDED(g->Open(&s))) {
                                s->BeginFigure(
                                    D2D1::Point2F(cb.left + S(5),
                                                  cb.top + S(11)),
                                    D2D1_FIGURE_BEGIN_HOLLOW);
                                s->AddLine(D2D1::Point2F(cb.left + S(9),
                                                         cb.top + S(15)));
                                s->AddLine(D2D1::Point2F(cb.left + S(17),
                                                         cb.top + S(6)));
                                s->EndFigure(D2D1_FIGURE_END_OPEN);
                                s->Close();
                                s->Release();
                            }
                            rt->DrawGeometry(g, m_bBg, S(2.5));
                            g->Release();
                        }
                    }
                }
                T(L"Launch Lunar Player when setup completes",
                  x0 + S(52), y + S(9), x0 + w - S(18), y + S(31),
                  m_bText, m_fBody);
                hit(L"toggle:launch",
                    R(x0 + S(8), y, x0 + w - S(8), y + S(44)));
                y += S(44);
            }
            T(L"Installation scope: All users (elevation required)", x0 + S(18),
              y, x0 + w - S(18), y + S(26), m_bDim, m_fSmall);
            y += S(26);
            T(L"Start Menu shortcut will be created", x0 + S(18), y,
              x0 + w - S(18), y + S(26), m_bDim, m_fSmall);
            y += S(26);
            T(L"Version " + m_version
                  + L" \u00B7 Alpha build \u00B7 Pre-release \u2014 bugs and incomplete features are expected.",
              x0 + S(18), y, x0 + w - S(18), y + S(26), m_bDim,
              m_fSmall);
            y += S(26) + S(12);
        }
        y += S(24);
    } else if (m_screen == NativeScreen::Installing) {
        T(L"Installing Lunar Player", x0, y, x0 + w, y + S(66), m_bText,
          m_fTitle);
        y += S(66);
        T(m_phase.empty() ? L"Working" : m_phase, x0, y, x0 + w,
          y + S(28), m_bDim, m_fSub);
        y += S(28);
        if (rt) {
            rt->FillRoundedRectangle(
                D2D1::RoundedRect(R(x0, yBase + y, x0 + w, yBase + y + S(10)),
                                  S(5), S(5)),
                m_bTrack);
            if (m_percent > 0) {
                const float fw = w * m_percent / 100.0f;
                rt->FillRoundedRectangle(
                    D2D1::RoundedRect(
                        R(x0, yBase + y, x0 + fw, yBase + y + S(10)),
                        S(5), S(5)),
                    m_bAccent);
            }
        }
        y += S(10) + S(12);
        T(m_detail, x0, y, x0 + w, y + S(28), m_bText, m_fBody);
        y += S(28);
        wchar_t pct[16]{};
        swprintf_s(pct, L"%lu%%", m_percent);
        if (rt)
            DrawText(rt, pct, R(x0, yBase + y, x0 + w, yBase + y + S(60)),
                     m_bText, m_fTitle);
        y += S(60) + S(24);
    } else if (m_screen == NativeScreen::Complete) {
        if (rt)
            DrawText(rt, L"\u2713",
                     R(x0, yBase + y, x0 + S(120), yBase + y + S(80)),
                     m_bAccent, m_fTitle);
        y += S(80);
        T(m_completeTitle, x0, y, x0 + w, y + S(60), m_bText,
          m_fTitle);
        y += S(60);
        T(m_completeDetail.empty()
              ? L"Installation completed successfully."
              : m_completeDetail,
          x0, y, x0 + w, y + S(28), m_bDim, m_fSub);
        y += S(28);
        if (!m_version.empty() && m_completeShowVersion)
            T(L"Version " + m_version, x0, y, x0 + w, y + S(28), m_bDim,
              m_fSub);
        y += S(28) + S(24);
    } else if (m_screen == NativeScreen::Maintenance) {
        if (rt) {
            DrawText(rt, L"Lunar Player ",
                     R(x0, yBase + y, x0 + S(260), yBase + y + S(66)),
                     m_bText, m_fTitle);
            IDWriteTextLayout* lo = nullptr;
            m_dw->CreateTextLayout(L"Lunar Player ", 13, m_fTitle,
                                   1000, 100, &lo);
            float adv = S(250);
            if (lo) {
                DWRITE_TEXT_METRICS tm{};
                if (SUCCEEDED(lo->GetMetrics(&tm)))
                    adv = tm.widthIncludingTrailingWhitespace;
                lo->Release();
            }
            DrawText(rt, L"is installed",
                     R(x0 + adv, yBase + y, x0 + w, yBase + y + S(66)),
                     m_bAccent, m_fTitle);
        }
        y += S(66);
        T(L"Version " + m_installedVersion
              + L" \u00B7 Alpha build \u00B7 Pre-release",
          x0, y, x0 + w, y + S(28), m_bDim, m_fSub);
        y += S(28);
        T(L"Installation Location", x0, y, x0 + w, y + S(26), m_bText,
          m_fHead);
        y += S(26);
        if (rt) {
            rt->FillRoundedRectangle(
                D2D1::RoundedRect(
                    R(x0, yBase + y, x0 + w, yBase + y + S(42)),
                    S(8), S(8)),
                m_bBox);
            rt->DrawRoundedRectangle(
                D2D1::RoundedRect(
                    R(x0, yBase + y, x0 + w, yBase + y + S(42)),
                    S(8), S(8)),
                m_bBorder, 1.0f);
        }
        T(m_installedPath, x0 + S(14), y + S(12), x0 + w - S(14),
          y + S(42), m_bText, m_fBody);
        y += S(42) + S(16);
        // Modify row (same style as the Advanced row).
        if (rt) {
            rt->DrawRoundedRectangle(
                D2D1::RoundedRect(R(x0, yBase + y, x0 + w,
                                            yBase + y + S(46)),
                                  S(8), S(8)),
                (m_hover == L"modify") ? m_bAccentHi : m_bBorder,
                1.0f);
        }
        T(L"\u2699  Modify components", x0 + S(18), y + S(13),
          x0 + w - S(40), y + S(38), m_bText, m_fBody);
        T(L"\u276F", x0 + w - S(44), y + S(13), x0 + w - S(18),
          y + S(38), m_bDim, m_fBody);
        hit(L"modify", R(x0, y, x0 + w, y + S(46)));
        y += S(46) + S(16);
        T(L"Repair reinstalls the application files. Uninstall removes "
          L"Lunar Player, its shortcuts and its registration.",
          x0, y, x0 + w, y + S(52), m_bDim, m_fSmall);
        y += S(52) + S(24);
    } else { // Error
        T(m_cancelled ? L"Cancelled" : L"Installation failed", x0, y,
          x0 + w, y + S(60), m_bText, m_fTitle);        y += S(60);
        T(m_error.empty()
              ? (m_cancelled ? L"The installation was cancelled before any changes were applied."
                             : L"An error occurred. Previous version (if any) was rolled back untouched.")
              : m_error,
          x0, y, x0 + w, y + S(76), m_bDim, m_fBody);
        y += S(76) + S(24);
    }
    return y;
}

void NativeInstallerWindow::OnPaint() {
    if (!EnsureDevice()) {
        PAINTSTRUCT ps{};
        BeginPaint(m_hwnd, &ps);
        EndPaint(m_hwnd, &ps);
        return;
    }
    PAINTSTRUCT ps{};
    BeginPaint(m_hwnd, &ps);
    RECT cr{};
    GetClientRect(m_hwnd, &cr);
    D2D1_SIZE_F size{ (float)(cr.right - cr.left),
                      (float)(cr.bottom - cr.top) };
    Layout(size);
    ID2D1RenderTarget* rt = m_rt;
    rt->BeginDraw();
    rt->Clear(C(0x0B1322));
    const double t = GetTickCount() / 1000.0;
    RenderSquares(rt, R(0, 0, size.width, size.height), t);
    RenderTitleBar(rt, size);
    // Viewport clip for scrollable content.
    const float viewTop = S(kTitleH);
    const float viewBot = size.height - S(100);
    rt->PushAxisAlignedClip(R(0, viewTop, size.width, viewBot),
                            D2D1_ANTIALIAS_MODE_ALIASED);
    ContentWalk(rt, m_layoutX, m_layoutW, viewTop - m_scrollY);
    rt->PopAxisAlignedClip();
    // Scrollbar.
    m_hasScroll = (m_scrollMax > 0.0f);
    if (m_hasScroll) {
        const float viewH = viewBot - viewTop;
        m_scrollTrack =
            R(size.width - S(14), viewTop + S(8), size.width - S(6),
              viewBot - S(8));
        rt->FillRoundedRectangle(
            D2D1::RoundedRect(m_scrollTrack, S(4), S(4)), m_bTrack);
        const float th =
            viewH * (viewH / m_contentH);
        const float ty = m_scrollTrack.top
            + (m_scrollY / m_contentH)
                * (m_scrollTrack.bottom - m_scrollTrack.top);
        rt->FillRoundedRectangle(
            D2D1::RoundedRect(
                R(m_scrollTrack.left, ty, m_scrollTrack.right, ty + th),
                S(4), S(4)),
            m_bBorder);
    } else {
        m_scrollTrack = R(0, 0, 0, 0);
    }
    RenderFooter(rt, size);
    // Keyboard focus ring.
    if (m_focusIdx >= 0 && m_focusIdx < (int)m_focusOrder.size()) {
        const std::wstring& fid = m_focusOrder[(size_t)m_focusIdx];
        for (auto& h : m_hits) {
            if (h.id != fid)
                continue;
            D2D1_RECT_F r = h.rc;
            if (!h.inFooter) {
                r.top += (viewTop - m_scrollY);
                r.bottom += (viewTop - m_scrollY);
                if (r.bottom < viewTop || r.top > viewBot)
                    break;
            }
            rt->DrawRoundedRectangle(
                D2D1::RoundedRect(
                    R(r.left - S(3), r.top - S(3), r.right + S(3),
                      r.bottom + S(3)),
                    S(10), S(10)),
                m_bAccentHi, S(1.5));
            break;
        }
    }
    HRESULT hr = rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
        DiscardDevice();
    EndPaint(m_hwnd, &ps);
}

void NativeInstallerWindow::RenderTitleBar(ID2D1RenderTarget* rt,
                                           D2D1_SIZE_F size) {
    const float th = S(kTitleH);
    // Hairline separator.
    rt->DrawLine(D2D1::Point2F(0, th), D2D1::Point2F(size.width, th),
                 m_bBorder, 1.0f);
    EnsureLogo();
    const float lx = m_layoutX > 0.0f ? m_layoutX : S(24);
    if (m_bmpLogo) {
        const D2D1_SIZE_F bs = m_bmpLogo->GetSize();
        const float s = S(26);
        rt->DrawBitmap(m_bmpLogo, R(lx, (th - s) / 2, lx + s,
                                    (th - s) / 2 + s * bs.height / bs.width));
    } else {
        DrawText(rt, L"\U0001F319",
                 R(lx, S(12), lx + S(30), th), m_bText, m_fSub);
    }
    const float tx = lx + S(34);
    const float tt = VCenter(0.0f, th, S(14));
    DrawText(rt, L"Lunar Player Installer",
             R(tx, tt, size.width - S(150), tt + S(24)), m_bDim, m_fSub);
    // Caption buttons.
    struct Btn {
        const wchar_t* id;
        D2D1_RECT_F rc;
    };
    Btn btns[3] = { { L"min", m_capMin },
                    { L"max", m_capMax },
                    { L"close", m_capClose } };
    const bool zoomed = IsZoomed(m_hwnd) ? true : false;
    for (auto& b : btns) {
        const bool hov = (m_hover == b.id);
        const bool prs = hov && m_pressed;
        std::wstring id(b.id);
        if (id == L"close" && hov)
            rt->FillRectangle(b.rc, m_bDanger);
        else if (hov)
            rt->FillRectangle(
                b.rc,
                prs ? m_bBorder : m_bTrack);
        ID2D1SolidColorBrush* g =
            (id == L"close" && hov) ? m_bText : m_bDim;
        const float cx = (b.rc.left + b.rc.right) / 2.0f;
        const float cyy = (b.rc.top + b.rc.bottom) / 2.0f;
        if (id == L"min") {
            rt->DrawLine(D2D1::Point2F(cx - S(5), cyy),
                         D2D1::Point2F(cx + S(5), cyy), g, S(1.2));
        } else if (id == L"max" && !zoomed) {
            rt->DrawRectangle(R(cx - S(5), cyy - S(5), cx + S(5),
                                cyy + S(5)),
                              g, S(1.2));
        } else if (id == L"max") {
            rt->DrawRectangle(R(cx - S(2), cyy - S(6), cx + S(6),
                                cyy + S(2)),
                              g, S(1.2));
            rt->DrawRectangle(R(cx - S(6), cyy - S(2), cx + S(2),
                                cyy + S(6)),
                              g, S(1.2));
        } else {
            rt->DrawLine(D2D1::Point2F(cx - S(5), cyy - S(5)),
                         D2D1::Point2F(cx + S(5), cyy + S(5)), g,
                         S(1.4));
            rt->DrawLine(D2D1::Point2F(cx - S(5), cyy + S(5)),
                         D2D1::Point2F(cx + S(5), cyy - S(5)), g,
                         S(1.4));
        }
    }
}

void NativeInstallerWindow::RenderFooter(ID2D1RenderTarget* rt,
                                         D2D1_SIZE_F size) {
    const float fy = size.height - S(100);
    rt->DrawLine(D2D1::Point2F(0, fy),
                 D2D1::Point2F(size.width, fy), m_bBorder, 1.0f);
    const float x0 = m_layoutX, w = m_layoutW;
    if (m_screen == NativeScreen::Install) {
        DrawText(rt, L"Required space:  " + FormatBytes(m_requiredBytes),
                 R(x0, fy + S(24), x0 + S(420), fy + S(48)), m_bText,
                 m_fBody);
        DrawText(rt, L"Available space: " + FormatBytes(m_availableBytes),
                 R(x0, fy + S(50), x0 + S(420), fy + S(74)), m_bText,
                 m_fBody);
        const bool cHov = (m_hover == L"cancel");
        const bool cPrs = cHov && m_pressed;
        rt->DrawRoundedRectangle(D2D1::RoundedRect(m_footCancel, S(8),
                                                   S(8)),
                                 cHov ? m_bText : m_bBorder, S(1.2));
        if (cPrs)
            rt->FillRoundedRectangle(
                D2D1::RoundedRect(m_footCancel, S(8), S(8)), m_bTrack);
        DrawText(rt, L"Cancel",
                 R(m_footCancel.left,
                   VCenter(m_footCancel.top,
                           m_footCancel.bottom - m_footCancel.top, S(15)),
                   m_footCancel.right, m_footCancel.bottom),
                 m_bText, m_fBtnC);
        const bool iHov = (m_hover == L"install");
        rt->FillRoundedRectangle(
            D2D1::RoundedRect(m_footInstall, S(8), S(8)),
            iHov ? m_bAccentHi : m_bAccent);
        DrawText(rt, L"Install  \u2192",
                 R(m_footInstall.left,
                   VCenter(m_footInstall.top,
                           m_footInstall.bottom - m_footInstall.top,
                           S(15)),
                   m_footInstall.right, m_footInstall.bottom),
                 m_bText, m_fBtnC);
    } else if (m_screen == NativeScreen::Installing) {
        wchar_t pct[16]{};
        swprintf_s(pct, L"%lu%%", m_percent);
        // Status sits RIGHT of Cancel (never inside/over it).
        DrawText(rt, (m_phase.empty() ? L"Working" : m_phase)
                         + L"  " + pct,
                 R(m_footCancel.right + S(16), fy + S(36), x0 + S(700),
                   fy + S(64)),
                 m_bText, m_fBody);
        const bool cHov = (m_hover == L"cancel");
        rt->DrawRoundedRectangle(D2D1::RoundedRect(m_footCancel, S(8),
                                                   S(8)),
                                 cHov ? m_bText : m_bBorder, S(1.2));
        DrawText(rt, L"Cancel",
                 R(m_footCancel.left,
                   VCenter(m_footCancel.top,
                           m_footCancel.bottom - m_footCancel.top, S(15)),
                   m_footCancel.right, m_footCancel.bottom),
                 m_bText, m_fBtnC);
    } else if (m_screen == NativeScreen::Complete) {
        DrawText(rt, L"Version " + m_version,
                 R(x0, fy + S(36), x0 + S(420), fy + S(64)), m_bDim,
                 m_fBody);
        const bool cHov = (m_hover == L"close2");
        rt->DrawRoundedRectangle(D2D1::RoundedRect(m_footCancel, S(8),
                                                   S(8)),
                                 cHov ? m_bText : m_bBorder, S(1.2));
        DrawText(rt, L"Close",
                 R(m_footCancel.left,
                   VCenter(m_footCancel.top,
                           m_footCancel.bottom - m_footCancel.top, S(15)),
                   m_footCancel.right, m_footCancel.bottom),
                 m_bText, m_fBtnC);
        // Uninstall completion omits Launch (nothing left to launch).
        if (m_completeUninstalled)
            return;
        const bool iHov = (m_hover == L"launch");
        rt->FillRoundedRectangle(
            D2D1::RoundedRect(m_footInstall, S(8), S(8)),
            iHov ? m_bAccentHi : m_bAccent);
        DrawText(rt, L"Launch",
                 R(m_footInstall.left,
                   VCenter(m_footInstall.top,
                           m_footInstall.bottom - m_footInstall.top,
                           S(15)),
                   m_footInstall.right, m_footInstall.bottom),
                 m_bText, m_fBtnC);
    } else if (m_screen == NativeScreen::Maintenance) {
        DrawText(rt, L"Version " + m_installedVersion,
                 R(x0, fy + S(36), x0 + S(260), fy + S(64)), m_bDim,
                 m_fBody);
        auto outline = [&](const D2D1_RECT_F& r,
                           const std::wstring& id,
                           const std::wstring& label) {
            const bool hov = (m_hover == id);
            const bool prs = hov && m_pressed;
            rt->DrawRoundedRectangle(D2D1::RoundedRect(r, S(8), S(8)),
                                     hov ? m_bText : m_bBorder,
                                     S(1.2));
            if (prs)
                rt->FillRoundedRectangle(
                    D2D1::RoundedRect(r, S(8), S(8)), m_bTrack);
            DrawText(rt, label,
                     R(r.left, VCenter(r.top, r.bottom - r.top, S(15)),
                       r.right, r.bottom),
                     m_bText, m_fBtnC);
        };
        outline(m_footCancel, L"mclose", L"Close");
        outline(m_footMid, L"repair", L"Repair");
        const bool uHov = (m_hover == L"uninstall");
        rt->FillRoundedRectangle(
            D2D1::RoundedRect(m_footInstall, S(8), S(8)),
            uHov ? m_bDanger : m_bAccent);
        DrawText(rt, L"Uninstall",
                 R(m_footInstall.left,
                   VCenter(m_footInstall.top,
                           m_footInstall.bottom - m_footInstall.top,
                           S(15)),
                   m_footInstall.right, m_footInstall.bottom),
                 m_bText, m_fBtnC);
    } else { // Error
        DrawText(rt,
                 m_cancelled ? L"Cancelled" : L"Installation failed",
                 R(x0, fy + S(36), x0 + S(420), fy + S(64)), m_bText,
                 m_fBody);
        const bool cHov = (m_hover == L"back");
        rt->FillRoundedRectangle(
            D2D1::RoundedRect(m_footCancel, S(8), S(8)),
            cHov ? m_bAccentHi : m_bAccent);
        DrawText(rt, L"Back",
                 R(m_footCancel.left,
                   VCenter(m_footCancel.top,
                           m_footCancel.bottom - m_footCancel.top, S(15)),
                   m_footCancel.right, m_footCancel.bottom),
                 m_bText, m_fBtnC);
    }
}

void NativeInstallerWindow::RenderSquares(ID2D1RenderTarget* rt,
                                          D2D1_RECT_F region, double t) {
    const float step = S(26);
    const float left = region.right * 0.55f;
    for (float y = region.top + S(8); y < region.bottom; y += step) {
        for (float x = left; x < region.right; x += step) {
            const int ix = (int)(x / step), iy = (int)(y / step);
            const unsigned h = Hash2(ix, iy);
            if ((h % 100) > 52)
                continue;
            const float edge =
                (x - left) / (region.right - left + 1.0f);
            const float fade = edge < 0.35f ? (edge / 0.35f) : 1.0f;
            const float tw =
                0.5f + 0.5f * sinf((float)t * (0.4f + (h % 7) * 0.12f)
                                   + (float)(h % 64));
            const float a = (0.10f + 0.55f * tw) * fade;
            if (a < 0.03f)
                continue;
            const float s = S(9) + (float)(h % 8) * m_scale;
            ID2D1SolidColorBrush* b = nullptr;
            rt->CreateSolidColorBrush(C(0x2F80ED, a * 255.0f), &b);
            if (b) {
                rt->FillRectangle(R(x, y, x + s, y + s), b);
                b->Release();
            }
        }
    }
}

void NativeInstallerWindow::DrawText(ID2D1RenderTarget* rt,
                                     const std::wstring& text,
                                     D2D1_RECT_F box, ID2D1Brush* brush,
                                     IDWriteTextFormat* fmt) {
    if (!rt || !fmt || text.empty())
        return;
    IDWriteTextLayout* layout = nullptr;
    if (FAILED(m_dw->CreateTextLayout(text.c_str(), (UINT32)text.size(),
                                      fmt, box.right - box.left,
                                      box.bottom - box.top, &layout)))
        return;
    rt->DrawTextLayout(D2D1::Point2F(box.left, box.top), layout, brush);
    layout->Release();
}

bool NativeInstallerWindow::EnsureDevice() {
    if (m_rt)
        return true;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 &m_d2d)))
        return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                   __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&m_dw))))
        return false;
    RECT r{};
    GetClientRect(m_hwnd, &r);
    D2D1_SIZE_U sz{ (UINT32)(r.right - r.left),
                    (UINT32)(r.bottom - r.top) };
    if (FAILED(m_d2d->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(m_hwnd, sz), &m_rt)))
        return false;
    m_rt->CreateSolidColorBrush(C(0x0B1322), &m_bBg);
    m_rt->CreateSolidColorBrush(C(0x111C33), &m_bCard);
    m_rt->CreateSolidColorBrush(C(0x22304D), &m_bBorder);
    m_rt->CreateSolidColorBrush(C(0xF1F5F9), &m_bText);
    m_rt->CreateSolidColorBrush(C(0x8CA0B8), &m_bDim);
    m_rt->CreateSolidColorBrush(C(0x2F80ED), &m_bAccent);
    m_rt->CreateSolidColorBrush(C(0x4A97FF), &m_bAccentHi);
    m_rt->CreateSolidColorBrush(C(0x1E2A45), &m_bTrack);
    m_rt->CreateSolidColorBrush(C(0x141F36), &m_bBox);
    m_rt->CreateSolidColorBrush(C(0xE81123), &m_bDanger);
    auto mk = [&](const wchar_t* fam, float px, bool bold,
                  IDWriteTextFormat** out) {
        m_dw->CreateTextFormat(
            fam, nullptr,
            bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            S(px), L"en-us", out);
        if (*out) {
            (*out)->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            DWRITE_TRIMMING trim{};
            trim.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
            (*out)->SetTrimming(&trim, nullptr);
        }
    };
    mk(L"Segoe UI", 44.0f, true, &m_fTitle);
    mk(L"Segoe UI", 14.0f, false, &m_fSub);
    mk(L"Segoe UI", 15.0f, true, &m_fHead);
    mk(L"Segoe UI", 13.5f, false, &m_fBody);
    mk(L"Segoe UI", 12.5f, false, &m_fSmall);
    mk(L"Segoe UI", 15.0f, true, &m_fBtn);
    mk(L"Segoe UI", 15.0f, true, &m_fBtnC);
    if (m_fBtnC)
        m_fBtnC->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    return m_rt != nullptr;
}

void NativeInstallerWindow::DiscardDevice() {
    auto rel = [](auto*& p) {
        if (p) {
            p->Release();
            p = nullptr;
        }
    };
    rel(m_bmpLogo);
    rel(m_fTitle);
    rel(m_fSub);
    rel(m_fHead);
    rel(m_fBody);
    rel(m_fSmall);
    rel(m_fBtn);
    rel(m_fBtnC);
    rel(m_bBg);
    rel(m_bCard);
    rel(m_bBorder);
    rel(m_bText);
    rel(m_bDim);
    rel(m_bAccent);
    rel(m_bAccentHi);
    rel(m_bTrack);
    rel(m_bBox);
    rel(m_bDanger);
    rel(m_rt);
    rel(m_dw);
    rel(m_d2d);
}

void NativeInstallerWindow::EnsureLogo() {
    if (m_bmpLogo || !m_rt || m_logoPath.empty())
        return;
    if (GetFileAttributesW(m_logoPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        return;
    IWICImagingFactory* wic = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&wic))))
        return;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    if (SUCCEEDED(wic->CreateDecoderFromFilename(
            m_logoPath.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnDemand, &dec))
        && SUCCEEDED(dec->GetFrame(0, &frame))
        && SUCCEEDED(wic->CreateFormatConverter(&conv))
        && SUCCEEDED(conv->Initialize(
               frame, GUID_WICPixelFormat32bppPBGRA,
               WICBitmapDitherTypeNone, nullptr, 0.0,
               WICBitmapPaletteTypeCustom))) {
        m_rt->CreateBitmapFromWicBitmap(conv, nullptr, &m_bmpLogo);
    }
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (dec) dec->Release();
    wic->Release();
}

// ---- System tray (logo icon, Show + real Exit) ----
namespace {
// Appends tray diagnostics to the shared installer log (NIM_ADD result,
// GetRect verification) — provable on the real EXE, not just in code.
void TrayDiag(const wchar_t* stage, HRESULT hr) {
    wchar_t log[MAX_PATH]{};
    DWORD nl = GetTempPathW(MAX_PATH, log);
    if (nl == 0 || nl >= MAX_PATH)
        return;
    wcscat_s(log, L"LunarInstall.log");
    FILE* f = nullptr;
    if (_wfopen_s(&f, log, L"a, ccs=UTF-8") != 0 || !f)
        return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    fwprintf_s(f, L"[%02d:%02d:%02d] tray stage=%s hr=0x%08X\n",
               st.wHour, st.wMinute, st.wSecond, stage,
               (unsigned)hr);
    fclose(f);
}
// Decodes the packaged LunarPlayer.png into an HICON for the tray.
// Falls back to the stock application icon when unavailable.
HICON LoadLogoIcon(const std::wstring& path) {
    if (path.empty()
        || GetFileAttributesW(path.c_str())
               == INVALID_FILE_ATTRIBUTES)
        return nullptr;
    IWICImagingFactory* wic = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&wic))))
        return nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    HICON icon = nullptr;
    if (SUCCEEDED(wic->CreateDecoderFromFilename(
            path.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnDemand, &dec))
        && SUCCEEDED(dec->GetFrame(0, &frame))
        && SUCCEEDED(wic->CreateFormatConverter(&conv))
        && SUCCEEDED(conv->Initialize(
               frame, GUID_WICPixelFormat32bppPBGRA,
               WICBitmapDitherTypeNone, nullptr, 0.0,
               WICBitmapPaletteTypeCustom))
        && SUCCEEDED(wic->CreateBitmapScaler(&scaler))
        && SUCCEEDED(scaler->Initialize(
               conv, GetSystemMetrics(SM_CXSMICON),
               GetSystemMetrics(SM_CYSMICON),
               WICBitmapInterpolationModeFant))) {
        HBITMAP color = nullptr, mask = nullptr;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = GetSystemMetrics(SM_CXSMICON);
        bi.bmiHeader.biHeight = -GetSystemMetrics(SM_CYSMICON);
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HDC dc = GetDC(nullptr);
        color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits,
                                 nullptr, 0);
        if (color && bits) {
            const UINT stride =
                (UINT)GetSystemMetrics(SM_CXSMICON) * 4;
            if (SUCCEEDED(scaler->CopyPixels(
                    nullptr, stride,
                    stride * (UINT)GetSystemMetrics(SM_CYSMICON),
                    (BYTE*)bits))) {
                mask = CreateBitmap(GetSystemMetrics(SM_CXSMICON),
                                    GetSystemMetrics(SM_CYSMICON), 1, 1,
                                    nullptr);
                ICONINFO ii{};
                ii.fIcon = TRUE;
                ii.hbmColor = color;
                ii.hbmMask = mask;
                icon = CreateIconIndirect(&ii);
                if (mask)
                    DeleteObject(mask);
                if (!icon && color)
                    DeleteObject(color);
                else if (icon) {
                    // Owned by the icon now.
                }
            } else {
                DeleteObject(color);
            }
        }
        ReleaseDC(nullptr, dc);
    }
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (dec) dec->Release();
    if (scaler) scaler->Release();
    wic->Release();
    return icon;
}
} // namespace

void NativeInstallerWindow::LogTray(const wchar_t* stage,
                                     HRESULT hr) {
    TrayDiag(stage, hr);
}

void NativeInstallerWindow::TrayVerify() {
    if (!m_trayAdded || !m_hwnd || m_trayVerified)
        return;
    m_trayVerified = true;
    NOTIFYICONIDENTIFIER nii{};
    nii.cbSize = sizeof(nii);
    nii.hWnd = m_hwnd;
    nii.uID = 1;
    RECT rc{};
    HRESULT hr = Shell_NotifyIconGetRect(&nii, &rc);
    TrayDiag(hr == S_OK ? L"verify-present" : L"verify-missing",
             hr);
}

void NativeInstallerWindow::TrayAdd() {
    if (m_trayAdded || !m_hwnd)
        return;
    // m_logoPath may not be populated yet (WM_CREATE precedes the
    // first RefreshData): resolve directly from the bridge.
    if (!m_hTrayIcon) {
        std::wstring lp = m_logoPath;
        if (lp.empty() && m_bridge)
            lp = m_bridge->LogoPath();
        m_hTrayIcon = LoadLogoIcon(lp);
        TrayDiag(lp.empty() ? L"logo-missing"
                            : (m_hTrayIcon ? L"logo-ok"
                                           : L"logo-decode-fail"),
                 m_hTrayIcon ? S_OK : E_FAIL);
    }
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    if (m_hTrayIcon) {
        nid.hIcon = m_hTrayIcon;
    } else {
        nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    wcscpy_s(nid.szTip, L"Lunar Player Installer");
    if (Shell_NotifyIconW(NIM_ADD, &nid)) {
        m_trayAdded = true;
        TrayDiag(L"add-ok", S_OK);
    } else {
        TrayDiag(L"add-fail",
                 HRESULT_FROM_WIN32(GetLastError()));
    }
}

void NativeInstallerWindow::TrayRemove() {
    if (!m_trayAdded || !m_hwnd)
        return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    m_trayAdded = false;
    if (m_hTrayIcon) {
        DestroyIcon(m_hTrayIcon);
        m_hTrayIcon = nullptr;
    }
}

void NativeInstallerWindow::TrayMenu(int x, int y) {
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;
    AppendMenuW(menu, MF_STRING, kTrayShow, L"Show Lunar Player Installer");
    AppendMenuW(menu, MF_STRING, kTrayExit, L"Exit Lunar Player Installer");
    SetForegroundWindow(m_hwnd);
    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON,
                   x, y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
}

void NativeInstallerWindow::EnsureWindowIcon() {
    if (m_hWndIcon || !m_hwnd)
        return;
    std::wstring lp;
    if (m_bridge)
        lp = m_bridge->LogoPath();
    if (!lp.empty())
        m_hWndIcon = LoadLogoIcon(lp);
    if (m_hWndIcon) {
        SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)m_hWndIcon);
        SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)m_hWndIcon);
    }
}
