#pragma once

// NativeInstallerWindow — dependency-free Win32 + Direct2D/DirectWrite
// installer UI. Borderless custom-chrome window (title bar + fixed
// footer + scrollable content viewport). No WebView2, no React.
//
// Layout (96-dpi base units, scaled by m_scale):
//   +-- custom title bar (logo, title, min/max/close) .... FIXED 52
//   +-- scrollable content viewport .................... FLEXIBLE
//   |     page title, location, components, advanced(+options)
//   +-- footer (space info + Cancel/Install) ........... FIXED 96
//
// Threading: everything runs on the STA UI thread that creates the
// window (Burn engine callbacks arrive marshaled via PostUi).

#include <d2d1.h>
#include <dwrite.h>
#include <stdint.h>
#include <functional>
#include <string>
#include <vector>
#include <windows.h>
#include <wincodec.h>

class InstallerBridge;

struct UiActions {
    std::function<void()> onInstall;
    std::function<void()> onCancel;
    std::function<void()> onBack;
    std::function<void()> onLaunch;
    std::function<void()> onExitTray;
    // Maintenance screen (standalone installer only).
    std::function<void()> onRepair;
    std::function<void()> onUninstall;
    std::function<void()> onModify;
};

enum class NativeScreen {
    Install,
    Installing,
    Complete,
    Error,
    Maintenance,
};

struct NativeComponentView {
    std::wstring id;
    std::wstring name;
    std::wstring description;
    std::wstring sizeLabel; // installed/on-disk size
    std::wstring dlLabel;   // e.g. "47.6 MB ↓ download" (empty if unknown)
    bool required = false;
    bool selected = false;
};

class NativeInstallerWindow {
public:
    NativeInstallerWindow(HINSTANCE hInstance, InstallerBridge* bridge,
                          UiActions actions);
    ~NativeInstallerWindow();

    bool Create(const std::wstring& title, int width, int height);
    void Show(int cmdShow = SW_SHOW);
    HWND Window() const { return m_hwnd; }

    // Engine-thread-marshaled state updates (call on UI thread).
    void RefreshData();
    void SetProgress(const std::wstring& phase,
                     const std::wstring& detail, DWORD percent);
    void SetComplete();
    // Completion with custom copy (e.g. uninstall result). Same layout,
    // same style; only the title/detail strings differ.
    void SetCompleteEx(const std::wstring& title,
                       const std::wstring& detail);
    // Uninstall completion: same Complete screen WITHOUT the Launch
    // button (the product no longer exists). Footer holds Close only.
    void SetUninstalled();
    // Maintenance screen: product detected (version + install path).
    void SetMaintenance(const std::wstring& version,
                        const std::wstring& installPath);
    void SetError(const std::wstring& message, bool cancelled);

    // Test harness: start directly on a screen with demo data.
    void SetTestScreen(NativeScreen s);
    // Backend UI action (uitest harness only): performs exactly what a
    // click on the given hit id would do, without touching the mouse.
    // Supported: toggle:<id>, advanced, install, cancel, back, launch,
    // close2, browse, path, repair, uninstall, modify, mclose.
    void TestAction(const std::wstring& id);
    // Demo progress driver (uitest harness only).
    void SetDemo(bool on) { m_demo = on; }

private:
    static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l);
    // Layout pass: fills m_hits (content coords), m_contentH, footer
    // rects. Paint uses it with the scroll offset applied.
    void Layout(D2D1_SIZE_F size);
    // Single content walk: draws when rt != null at yBase offset,
    // always records content-coord hits, returns end-y (content coords).
    float ContentWalk(ID2D1RenderTarget* rt, float x0, float w,
                      float yBase);
    void OnPaint();
    void OnClick(int x, int y);
    void OnMove(int x, int y);
    void OnWheel(int delta);
    void OnKey(int vk, bool shift);
    void ActivateHit(const std::wstring& id);
    void TrayAdd();
    void TrayRemove();
    void TrayMenu(int x, int y);
    void TrayVerify(); // one-shot GetRect self-check (logged)
    static void LogTray(const wchar_t* stage, HRESULT hr);
    void EnsureWindowIcon();
    void RenderTitleBar(ID2D1RenderTarget* rt, D2D1_SIZE_F size);
    void RenderFooter(ID2D1RenderTarget* rt, D2D1_SIZE_F size);
    void RenderSquares(ID2D1RenderTarget* rt, D2D1_RECT_F region,
                       double t);
    void DrawText(ID2D1RenderTarget* rt, const std::wstring& text,
                  D2D1_RECT_F box, ID2D1Brush* brush,
                  IDWriteTextFormat* fmt);
    bool EnsureDevice();
    void DiscardDevice();
    void EnsureLogo();
    std::wstring FormatBytes(uint64_t bytes);
    std::wstring FormatDownload(uint64_t bytes);
    float S(float v) const { return v * m_scale; }

    struct Hit {
        std::wstring id;
        D2D1_RECT_F rc; // content coords (add m_scrollY + viewTop)
        bool inFooter = false;
    };

    HINSTANCE m_hInstance;
    HWND m_hwnd;
    InstallerBridge* m_bridge; // not owned
    UiActions m_actions;

    NativeScreen m_screen = NativeScreen::Install;
    std::vector<NativeComponentView> m_components;
    std::wstring m_installPath;
    std::wstring m_version;
    uint64_t m_requiredBytes = 0;
    uint64_t m_availableBytes = 0;
    bool m_showAdvanced = false;
    bool m_launchAfter = false;
    std::wstring m_hover;
    bool m_pressed = false;

    std::wstring m_phase;
    std::wstring m_detail;
    DWORD m_percent = 0;
    // Complete-screen copy (defaults = install success).
    std::wstring m_completeTitle = L"Lunar Player is ready";
    std::wstring m_completeDetail =
        L"Installation completed successfully.";
    bool m_completeShowVersion = true;
    // Uninstall completion: Launch button omitted (nothing to launch).
    bool m_completeUninstalled = false;
    // Maintenance-screen data.
    std::wstring m_installedVersion;
    std::wstring m_installedPath;
    std::wstring m_error;
    bool m_cancelled = false;
    bool m_demo = false;

    std::vector<Hit> m_hits;
    std::vector<std::wstring> m_focusOrder;
    int m_focusIdx = -1;
    float m_contentH = 0.0f;
    float m_scrollY = 0.0f;
    float m_scrollMax = 0.0f;
    float m_layoutX = 0.0f;
    float m_layoutW = 0.0f;
    D2D1_RECT_F m_scrollTrack{};
    bool m_hasScroll = false;
    float m_scale = 1.0f;
    // Footer rects (screen coords).
    D2D1_RECT_F m_footCancel{};
    D2D1_RECT_F m_footInstall{};
    D2D1_RECT_F m_footMid{}; // maintenance Repair button
    // Title-bar rects (screen coords).
    D2D1_RECT_F m_capMin{};
    D2D1_RECT_F m_capMax{};
    D2D1_RECT_F m_capClose{};
    D2D1_RECT_F m_capDrag{};

    ID2D1Factory* m_d2d = nullptr;
    IDWriteFactory* m_dw = nullptr;
    ID2D1HwndRenderTarget* m_rt = nullptr;
    ID2D1SolidColorBrush* m_bBg = nullptr;
    ID2D1SolidColorBrush* m_bCard = nullptr;
    ID2D1SolidColorBrush* m_bBorder = nullptr;
    ID2D1SolidColorBrush* m_bText = nullptr;
    ID2D1SolidColorBrush* m_bDim = nullptr;
    ID2D1SolidColorBrush* m_bAccent = nullptr;
    ID2D1SolidColorBrush* m_bAccentHi = nullptr;
    ID2D1SolidColorBrush* m_bTrack = nullptr;
    ID2D1SolidColorBrush* m_bBox = nullptr;
    ID2D1SolidColorBrush* m_bDanger = nullptr;
    ID2D1Bitmap* m_bmpLogo = nullptr;
    HICON m_hTrayIcon = nullptr;
    HICON m_hWndIcon = nullptr;
    bool m_trayAdded = false;
    bool m_trayVerified = false;
    int m_trayRetries = 0;
    UINT m_taskbarMsg = 0;
    std::wstring m_logoPath;
    IDWriteTextFormat* m_fTitle = nullptr;
    IDWriteTextFormat* m_fSub = nullptr;
    IDWriteTextFormat* m_fHead = nullptr;
    IDWriteTextFormat* m_fBody = nullptr;
    IDWriteTextFormat* m_fSmall = nullptr;
    IDWriteTextFormat* m_fBtn = nullptr;
    IDWriteTextFormat* m_fBtnC = nullptr; // centered (button labels)
};
