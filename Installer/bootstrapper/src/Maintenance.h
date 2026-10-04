#pragma once

// Maintenance — install registration + repair/uninstall backend for the
// standalone installer (no Burn engine). All machine-state changes for
// the external-pack flow funnel here with per-operation HRESULTs so the
// caller can log the exact failing operation (never a bare 0x80004005).
//
// ARP: HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\
//   LunarPlayer (64-bit view from the x64 installer). The uninstaller
//   is a copy of the installer itself
//   (<install>\LunarPlayerUninstaller.exe) run with --uninstall.

#include <functional>
#include <string>
#include <vector>
#include <windows.h>

namespace Maintenance {

// Phase reporter for long operations (phase + detail text for UI).
using PhaseFn = std::function<void(const wchar_t* phase,
                                   const wchar_t* detail)>;

// Directory of the running installer image (sidecar home:
// payload-sizes.json, LunarPlayer.png, 7z.exe live next to the exe).
std::wstring ExeDir();

// True when a previous install is registered (ARP key) or evident
// (install.json + player exe). Returns install path + version.
bool IsInstalled(std::wstring& installPath, std::wstring& version);

// Windows Installed Apps registration (HKLM Uninstall key).
HRESULT RegisterARP(const std::wstring& installPath,
                    const std::wstring& version,
                    uint64_t estimatedSizeKB);
HRESULT UnregisterARP();

// Copies the running installer next to the product as the persistent
// uninstaller (ARP UninstallString target).
HRESULT CopySelfAsUninstaller(const std::wstring& installPath);
std::wstring UninstallerPath(const std::wstring& installPath);

// Installed-files record (<install>\installed-files.json): exact pack
// file lists recorded at install time so uninstall/repair remove
// exactly what was installed.
HRESULT WriteInstalledFiles(const std::wstring& installPath,
                            const std::vector<std::wstring>& coreFiles,
                            const std::vector<std::wstring>& aiFiles,
                            bool aiInstalled);
HRESULT ReadInstalledFiles(const std::wstring& installPath,
                           std::vector<std::wstring>& coreFiles,
                           std::vector<std::wstring>& aiFiles,
                           bool& aiInstalled);

// Full removal: files (by record) -> shortcuts -> ARP -> install.json
// -> record -> uninstaller self-schedule. Refuses while the player runs.
HRESULT RemoveInstallation(const std::wstring& installPath,
                           const PhaseFn& phase,
                           volatile LONG* cancelFlag);

// Pack file lists from the sidecar payload-sizes.json
// ("coreFiles"/"aiFiles": flattened relative paths). Used for exact
// install attribution; absent lists make the caller fall back.
HRESULT PackFileLists(std::vector<std::wstring>& coreFiles,
                      std::vector<std::wstring>& aiFiles);

// True while LunarPlayer.exe runs (uninstall must refuse then).
bool PlayerRunning();

// Extractor resolution: LUNAR_SEVENZIP -> exe-dir sidecar -> PF 7-Zip.
// Empty when none exists (caller fails fast with a clear message).
std::wstring FindExtractor();

// VC++ 2022 x64 runtime presence (same key Burn probes).
bool VcRedistPresent();
// Downloads (redirect-following) + quiet-installs the runtime.
// Non-fatal philosophy lives with the caller; this reports honestly.
HRESULT InstallVcRedist(const std::wstring& dlDir, const PhaseFn& phase,
                        volatile LONG* cancelFlag);

} // namespace Maintenance
