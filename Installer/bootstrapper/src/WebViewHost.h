#pragma once

// WebViewHost — owns the installer window + WebView2 controller.
// Bridge-agnostic: any IRpcBackend (installer, updater) can be hosted.
// - Window: standard overlapped window with title bar.
// - WebView2 Evergreen; user-data dir under %LOCALAPPDATA%\Lunar Player.
// - Serves the built React UI (dist/index.html) via virtual host in
//   production; vite dev-server URL when LUNAR_INSTALLER_DEV_URL is set.
// - Forwards React postMessage RPC to the backend; posts events back.

#include <string>
#include <windows.h>
#include <Unknwn.h>

#include "WebView2.h"

#include "RpcBackend.h" // IRpcBackend (narrow RPC contract)

class WebViewHost {
public:
    WebViewHost(HINSTANCE hInstance, IRpcBackend* backend);
    ~WebViewHost();

    bool Create(const std::wstring& title, int width, int height);
    void Show(int cmdShow = SW_SHOW);
    HWND Window() const { return m_hwnd; }
    // Query string appended to the UI URL (e.g. L"?mode=updater").
    // Must be set before Create().
    void SetQuery(const std::wstring& q) { m_query = q; }

    // Native -> React: { type: 'progress', progress: {...} } etc.
    void PostEvent(const std::wstring& json);

    // React -> native dispatch (called from the WebMessage handler).
    void OnWebMessage(const std::wstring& json);

private:
    static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l);
    void InitWebView();
    void OnWebViewReady(HRESULT hr);
    std::wstring UiUrl() const;

    HINSTANCE m_hInstance;
    HWND m_hwnd;
    std::wstring m_query;
    IRpcBackend* m_backend; // not owned (BA/updater lifetime)
    ICoreWebView2Controller* m_controller;
    ICoreWebView2* m_web;
};
