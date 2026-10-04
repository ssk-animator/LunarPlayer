// LunarPlayerUpdater.exe — entry point. Window + WebView2 host serving
// the shared React bundle in updater mode (?mode=updater). Message pump
// on the STA thread; the bridge does network/crypto/process work on
// worker threads where blocking (download loop pumps via callback).

#include "UpdaterBridge.h"
#include "WebViewHost.h"

#include <windows.h>

int RunUpdater(HINSTANCE hInstance, int nShow);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR cmdLine, int nShow) {
    if (cmdLine
        && (wcsstr(cmdLine, L"--self-test") || wcsstr(cmdLine, L"/selftest")))
        return UpdaterBridge::SelfTest();
    {
        // Live fixture flow (test driver only): LUNAR_UPDATER_TEST_DIR
        // points at a fixture dir with install.json; LUNAR_UPDATE_URL at
        // the fixture manifest. Headless, exit-coded, no UI.
        wchar_t dir[32768]{};
        DWORD n = GetEnvironmentVariableW(L"LUNAR_UPDATER_TEST_DIR", dir,
                                          32768);
        if (n > 0 && n < 32768) {
            if (!GetConsoleWindow()) {
                AllocConsole();
                FILE* f = nullptr;
                freopen_s(&f, "CONOUT$", "w", stdout);
                freopen_s(&f, "CONOUT$", "w", stderr);
            }
            HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            if (FAILED(hr))
                return 3;
            const int rc = UpdaterBridge::FixtureTest(dir);
            CoUninitialize();
            return rc;
        }
    }
    return RunUpdater(hInstance, nShow);
}

int RunUpdater(HINSTANCE hInstance, int nShow) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInit = SUCCEEDED(hr);

    wchar_t mod[MAX_PATH]{};
    GetModuleFileNameW(nullptr, mod, MAX_PATH);
    std::wstring dir(mod);
    const size_t slash = dir.find_last_of(L"\\/");
    dir = (slash == std::wstring::npos) ? L"." : dir.substr(0, slash);

    UpdaterBridge bridge(hInstance, dir);
    WebViewHost host(hInstance, &bridge);
    host.SetQuery(L"?mode=updater");
    // Progress events from the download loop -> React.
    bridge.SetProgressCallback([&host](uint64_t received, uint64_t total) {
        wchar_t msg[256]{};
        swprintf_s(msg, L"{\"type\":\"progress\",\"progress\":"
                        L"{\"phase\":\"Downloading\",\"detail\":\"\","
                        L"\"received\":%llu,\"total\":%llu,\"percent\":%d}}",
                   received, total,
                   total > 0 ? (int)(received * 100 / total) : 0);
        host.PostEvent(msg);
    });
    if (!host.Create(L"Lunar Player Updater", 720, 520)) {
        if (comInit)
            CoUninitialize();
        return 1;
    }
    host.Show(nShow);

    MSG m{};
    int exitCode = 0;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (comInit)
        CoUninitialize();
    return exitCode;
}
