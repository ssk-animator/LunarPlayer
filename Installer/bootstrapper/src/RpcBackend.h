#pragma once

// IRpcBackend — narrow native UI contract shared by the installer and
// updater hosts. WebView-free: the native Direct2D installer calls the
// bridge directly; the WebView-based updater uses it through
// WebViewHost::OnWebMessage. Dispatch(method, argsJson) returns
// JSON-encoded result or throws a JSON-encoded error string.
#include <string>

struct IRpcBackend {
    virtual ~IRpcBackend() = default;
    virtual std::wstring Dispatch(const std::wstring& method,
                                  const std::wstring& argsJson) = 0;
};
