// BootstrapperApplication entry points (WiX Burn v3 native BA contract,
// verified against IBootstrapperApplication.h in the v3.14 SDK and proven
// by the engine log: it GetProcAddress's "BootstrapperApplicationCreate").
//
//   BootstrapperApplicationCreate(engine, command) -> new IBootstrapperApplication
//   BootstrapperApplicationDestroy()              -> teardown hook
//
// The engine owns the BA lifetime: Create on load, Release when done.

#include "LunarBA.h"

#include <windows.h>

static LunarBA* g_ba = nullptr;

extern "C" __declspec(dllexport) HRESULT WINAPI
BootstrapperApplicationCreate(__in IBootstrapperEngine* pEngine,
                              __in const BOOTSTRAPPER_COMMAND* pCommand,
                              __out IBootstrapperApplication** ppApplication) {
    if (!pEngine || !ppApplication)
        return E_INVALIDARG;
    if (g_ba)
        return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    // NOTE: no CoInitializeEx here — the engine owns this thread's COM
    // apartment (MTA here; STA would fail with RPC_E_CHANGED_MODE). Our
    // STA UI thread initializes COM itself in UiThreadProc (WebView2
    // requires STA).
    HINSTANCE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                           | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&g_ba), &self);
    g_ba = new LunarBA(self ? self : GetModuleHandleW(nullptr), pEngine,
                       pCommand);
    *ppApplication = static_cast<IBootstrapperApplication*>(g_ba);
    return S_OK;
}

extern "C" __declspec(dllexport) void WINAPI BootstrapperApplicationDestroy() {
    g_ba = nullptr;
}

BOOL APIENTRY DllMain(HINSTANCE h, DWORD reason, LPVOID /*r*/) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(h);
    return TRUE;
}
