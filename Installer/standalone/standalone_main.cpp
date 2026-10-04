// LunarPlayerInstaller — standalone installer host (no Burn engine).
// requireAdministrator manifest: single elevated process from startup.
// UI mode: native Direct2D installer (first install or maintenance).
// --uninstall [--quiet]: silent record-driven removal (ARP target).

#include "LunarBA.h"
#include "Maintenance.h"

#include <shellapi.h>
#include <stdio.h>

namespace {

void LogLine(const wchar_t* stage, HRESULT hr) {
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
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    fwprintf_s(f, L"[%02d:%02d:%02d] standalone stage=%s exe=%s "
                  L"hr=0x%08X\n",
               st.wHour, st.wMinute, st.wSecond, stage, exe,
               (unsigned)hr);
    fclose(f);
}

bool HasArg(const wchar_t* cmd, const wchar_t* token) {
    return cmd && wcsstr(cmd, token) != nullptr;
}

int RunSilentUninstall(bool quiet) {
    std::wstring path, ver;
    if (!Maintenance::IsInstalled(path, ver)) {
        LogLine(L"uninstall-not-installed", S_OK);
        if (!quiet)
            MessageBoxW(nullptr,
                        L"Lunar Player does not appear to be installed.",
                        L"Lunar Player Installer", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    LONG cancel = 0;
    HRESULT hr = Maintenance::RemoveInstallation(
        path,
        [&](const wchar_t*, const wchar_t*) {},
        &cancel);
    LogLine(L"uninstall-silent-end", hr);
    if (FAILED(hr) && !quiet) {
        wchar_t msg[256]{};
        swprintf_s(msg, L"Uninstall failed (0x%08lX).", (long)hr);
        MessageBoxW(nullptr, msg, L"Lunar Player Installer",
                    MB_OK | MB_ICONERROR);
    }
    return FAILED(hr) ? 1 : 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR cmdLine,
                    int nShow) {
    (void)nShow;
    const bool uninstall = HasArg(cmdLine, L"--uninstall");
    const bool quiet = HasArg(cmdLine, L"--quiet");
    if (uninstall)
        return RunSilentUninstall(quiet);

    // Interactive install/maintenance. Fabricated Burn command: the BA
    // only reads InitialAction (UNKNOWN = interactive, shows UI).
    static BOOTSTRAPPER_COMMAND cmd{};
    cmd.action = BOOTSTRAPPER_ACTION_UNKNOWN;
    LunarBA ba(hInstance, nullptr, &cmd);
    HRESULT hr = ba.OnStartup();
    if (FAILED(hr)) {
        LogLine(L"standalone-startup-fail", hr);
        return 1;
    }
    HANDLE uiThread = ba.UiThread();
    if (!uiThread) {
        // Second instance (focused existing window) or headless path:
        // nothing to wait for.
        LogLine(L"standalone-no-ui-thread", S_OK);
        return 0;
    }
    WaitForSingleObject(uiThread, INFINITE);
    LogLine(L"standalone-exit", S_OK);
    return 0;
}
