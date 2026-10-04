// uitest — headless visual harness for the native installer UI.
// No Burn engine (null backend): exercises layout, input, and screens
// for screenshot verification. Usage: uitest.exe [install|installing|
// complete|error]. Demo Install/Cancel buttons animate locally.

#include "InstallerBridge.h"
#include "NativeUi.h"

#include <string>
#include <vector>
#include <windows.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR cmdLine,
                    int nShow) {
    (void)nShow;
    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hrCom))
        return 2;

    InstallerBridge bridge(nullptr);
    UiActions demo;
    NativeInstallerWindow* ui = nullptr;
    demo.onInstall = [&ui]() {
        if (ui) ui->SetDemo(true);
    };
    demo.onCancel = [&ui]() {
        if (ui) {
            ui->SetDemo(false);
            ui->SetError(L"", true);
        }
    };
    demo.onBack = [&ui]() {
        if (ui) {
            ui->SetDemo(false);
            ui->SetTestScreen(NativeScreen::Install);
            ui->RefreshData();
        }
    };
    demo.onLaunch = []() {};
    ui = new NativeInstallerWindow(hInstance, &bridge, demo);
    if (!ui->Create(L"Lunar Player Installer", 1280, 800))
        return 1;

    std::wstring mode = cmdLine ? cmdLine : L"";
    while (!mode.empty() && iswspace(mode.front()))
        mode.erase(mode.begin());
    // First token = screen; remaining tokens = backend UI actions
    // (no mouse needed): toggle:<id>, advanced, scrolldown, scrollup,
    // install, cancel, back, launch.
    std::wstring screen = mode;
    std::vector<std::wstring> actions;
    {
        size_t p = mode.find(L' ');
        if (p != std::wstring::npos) {
            screen = mode.substr(0, p);
            std::wstring rest = mode.substr(p + 1);
            size_t s = 0;
            while (s < rest.size()) {
                while (s < rest.size() && iswspace(rest[s]))
                    ++s;
                size_t e = s;
                while (e < rest.size() && !iswspace(rest[e]))
                    ++e;
                if (e > s)
                    actions.push_back(rest.substr(s, e - s));
                s = e;
            }
        }
        while (!screen.empty() && iswspace(screen.back()))
            screen.pop_back();
    }
    if (screen == L"installing") {
        ui->SetTestScreen(NativeScreen::Installing);
        ui->SetDemo(true);
    } else if (screen == L"complete") {
        ui->SetTestScreen(NativeScreen::Complete);
    } else if (screen == L"error") {
        ui->SetError(
            L"Could not download LunarPlayer.msi (0x80070002). Check the connection and retry.",
            false);
    }
    ui->Show();
    for (auto& a : actions)
        ui->TestAction(a);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    delete ui;
    CoUninitialize();
    return 0;
}
