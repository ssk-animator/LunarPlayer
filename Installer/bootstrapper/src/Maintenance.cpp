// Maintenance implementation. See Maintenance.h for the contract.
// No Burn engine, no MSI: plain Win32 registry/filesystem/process APIs.
// Every public function returns an HRESULT naming the failing operation.

#include "Maintenance.h"

#include "BundleVersion.h"
#include "Downloader.h"

#include <knownfolders.h>
#include <objbase.h>
#include <shlobj.h>
#include <stdio.h>
#include <tlhelp32.h>

namespace {

constexpr wchar_t kArpKey[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
    L"LunarPlayer";
constexpr wchar_t kUninstallerFile[] = L"LunarPlayerUninstaller.exe";
constexpr wchar_t kVcRedistUrl[] =
    L"https://aka.ms/vs/17/release/vc_redist.x64.exe";
constexpr wchar_t kVcKey[] =
    L"SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x64";

bool FileExists(const std::wstring& p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Minimal flat-JSON helpers for our own small known-shape files.
std::wstring JsonQuoted(const std::wstring& json, const wchar_t* key) {
    std::wstring k = L"\"";
    k += key;
    k += L"\":";
    const wchar_t* p = wcsstr(json.c_str(), k.c_str());
    if (!p)
        return L"";
    p += k.size();
    while (*p == L' ' || *p == L'\t')
        ++p;
    if (*p != L'"')
        return L"";
    const wchar_t* e = wcschr(p + 1, L'"');
    if (!e)
        return L"";
    return std::wstring(p + 1, e);
}

std::wstring ReadAllText(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return L"";
    LARGE_INTEGER sz{};
    std::string raw;
    if (GetFileSizeEx(h, &sz) && sz.QuadPart > 0
        && sz.QuadPart < 4 * 1024 * 1024) {
        raw.resize((size_t)sz.QuadPart);
        DWORD n = 0;
        if (!ReadFile(h, raw.data(), (DWORD)raw.size(), &n, nullptr)
            || n != raw.size())
            raw.clear();
    }
    CloseHandle(h);
    std::wstring out;
    // Installed payload paths are ASCII; try UTF-8 first, else ANSI.
    int wn = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                 raw.data(), (int)raw.size(), nullptr,
                                 0);
    if (wn > 0) {
        out.resize((size_t)wn);
        MultiByteToWideChar(CP_UTF8, 0, raw.data(), (int)raw.size(),
                            out.data(), wn);
    } else if (!raw.empty()) {
        wn = MultiByteToWideChar(CP_ACP, 0, raw.data(),
                                 (int)raw.size(), nullptr, 0);
        if (wn > 0) {
            out.resize((size_t)wn);
            MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)raw.size(),
                                out.data(), wn);
        }
    }
    return out;
}

bool WriteAllText(const std::wstring& path, const std::string& utf8) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD w = 0;
    bool ok = !!WriteFile(h, utf8.data(), (DWORD)utf8.size(), &w,
                          nullptr)
        && w == utf8.size();
    CloseHandle(h);
    return ok;
}

std::string ToUtf8(const std::wstring& w) {
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0,
                                nullptr, nullptr);
    std::string o;
    if (n > 1) {
        o.resize((size_t)n - 1);
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, o.data(), n,
                            nullptr, nullptr);
    }
    return o;
}

LONG SetSz(HKEY k, const wchar_t* name, const std::wstring& v) {
    return RegSetValueExW(k, name, 0, REG_SZ,
                          reinterpret_cast<const BYTE*>(v.c_str()),
                          (DWORD)((v.size() + 1) * sizeof(wchar_t)));
}

LONG SetDword(HKEY k, const wchar_t* name, DWORD v) {
    return RegSetValueExW(k, name, 0, REG_DWORD,
                          reinterpret_cast<const BYTE*>(&v),
                          sizeof(v));
}

} // namespace

namespace Maintenance {

std::wstring ExeDir() {
    wchar_t mod[MAX_PATH]{};
    GetModuleFileNameW(nullptr, mod, MAX_PATH);
    std::wstring d(mod);
    const size_t s = d.find_last_of(L"\\/");
    return s == std::wstring::npos ? L"." : d.substr(0, s);
}

bool IsInstalled(std::wstring& installPath, std::wstring& version) {
    // Authoritative: ARP registration.
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kArpKey, 0,
                      KEY_QUERY_VALUE, &k)
        == ERROR_SUCCESS) {
        wchar_t loc[MAX_PATH]{}, ver[64]{};
        DWORD lt = sizeof(loc), vt = sizeof(ver);
        DWORD t1 = 0, t2 = 0;
        bool hasLoc = RegQueryValueExW(k, L"InstallLocation", nullptr,
                                       &t1, (BYTE*)loc, &lt)
                == ERROR_SUCCESS
            && t1 == REG_SZ;
        bool hasVer = RegQueryValueExW(k, L"DisplayVersion", nullptr,
                                       &t2, (BYTE*)ver, &vt)
                == ERROR_SUCCESS
            && t2 == REG_SZ;
        RegCloseKey(k);
        if (hasLoc && loc[0]
            && FileExists(std::wstring(loc) + L"\\LunarPlayer.exe")) {
            installPath.assign(loc);
            version.assign(hasVer && ver[0] ? ver
                                            : LUNAR_BUNDLE_VERSION);
            return true;
        }
    }
    // Fallback: install.json evidence (pre-ARP or repaired installs).
    wchar_t* base = nullptr;
    std::wstring def = L"C:\\Program Files\\Lunar Player";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFilesX64, 0,
                                       nullptr, &base))) {
        def.assign(base);
        def += L"\\Lunar Player";
        CoTaskMemFree(base);
    }
    const std::wstring ij = def + L"\\install.json";
    if (FileExists(def + L"\\LunarPlayer.exe") && FileExists(ij)) {
        installPath = def;
        std::wstring v = JsonQuoted(ReadAllText(ij), L"version");
        version = v.empty() ? std::wstring(LUNAR_BUNDLE_VERSION) : v;
        return true;
    }
    return false;
}

HRESULT RegisterARP(const std::wstring& installPath,
                    const std::wstring& version,
                    uint64_t estimatedSizeKB) {
    HKEY k = nullptr;
    LONG lr = RegCreateKeyExW(HKEY_LOCAL_MACHINE, kArpKey, 0, nullptr,
                              REG_OPTION_NON_VOLATILE,
                              KEY_SET_VALUE, nullptr, &k, nullptr);
    if (lr != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(lr);
    const std::wstring uninst = UninstallerPath(installPath);
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t date[16]{};
    swprintf_s(date, L"%04d%02d%02d", st.wYear, st.wMonth, st.wDay);
    int major = 0, minor = 0;
    swscanf_s(version.c_str(), L"%d.%d", &major, &minor);
    HRESULT hr = S_OK;
    if ((lr = SetSz(k, L"DisplayName", L"Lunar Player"))
            != ERROR_SUCCESS
        || (lr = SetSz(k, L"DisplayVersion", version))
            != ERROR_SUCCESS
        || (lr = SetSz(k, L"Publisher", L"SSK")) != ERROR_SUCCESS
        || (lr = SetSz(k, L"InstallLocation", installPath))
            != ERROR_SUCCESS
        || (lr = SetSz(k, L"DisplayIcon",
                        installPath + L"\\LunarPlayer.exe,0"))
            != ERROR_SUCCESS
        || (lr = SetSz(k, L"UninstallString",
                        L"\"" + uninst + L"\" --uninstall"))
            != ERROR_SUCCESS
        || (lr = SetSz(k, L"QuietUninstallString",
                        L"\"" + uninst + L"\" --uninstall --quiet"))
            != ERROR_SUCCESS
        || (lr = SetSz(k, L"ModifyPath", L"\"" + uninst + L"\""))
            != ERROR_SUCCESS
        || (lr = SetSz(k, L"URLInfoAbout",
                        L"https://github.com/ssk-animator/LunarPlayer"))
            != ERROR_SUCCESS
        || (lr = SetSz(k, L"InstallDate", date)) != ERROR_SUCCESS
        || (lr = SetDword(k, L"EstimatedSize",
                           estimatedSizeKB > 0xFFFFFFFF
                               ? 0xFFFFFFFF
                               : (DWORD)estimatedSizeKB))
            != ERROR_SUCCESS
        || (lr = SetDword(k, L"VersionMajor", (DWORD)major))
            != ERROR_SUCCESS
        || (lr = SetDword(k, L"VersionMinor", (DWORD)minor))
            != ERROR_SUCCESS
        || (lr = SetDword(k, L"NoModify", 0)) != ERROR_SUCCESS
        || (lr = SetDword(k, L"NoRepair", 1)) != ERROR_SUCCESS
        || (lr = SetDword(k, L"NoRemove", 0)) != ERROR_SUCCESS) {
        hr = HRESULT_FROM_WIN32(lr);
    }
    RegCloseKey(k);
    if (FAILED(hr))
        UnregisterARP();
    return hr;
}

HRESULT UnregisterARP() {
    LONG lr = RegDeleteKeyW(HKEY_LOCAL_MACHINE, kArpKey);
    if (lr == ERROR_SUCCESS || lr == ERROR_FILE_NOT_FOUND)
        return S_OK;
    return HRESULT_FROM_WIN32(lr);
}

std::wstring UninstallerPath(const std::wstring& installPath) {
    return installPath + L"\\" + kUninstallerFile;
}

HRESULT CopySelfAsUninstaller(const std::wstring& installPath) {
    wchar_t self[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, self, MAX_PATH))
        return HRESULT_FROM_WIN32(GetLastError());
    const std::wstring dst = UninstallerPath(installPath);
    if (_wcsicmp(self, dst.c_str()) == 0)
        return S_OK; // already the installed uninstaller
    if (!CopyFileW(self, dst.c_str(), FALSE))
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

HRESULT WriteInstalledFiles(const std::wstring& installPath,
                            const std::vector<std::wstring>& coreFiles,
                            const std::vector<std::wstring>& aiFiles,
                            bool aiInstalled) {
    std::string j = "{\"schema\":1,\"aiInstalled\":";
    j += aiInstalled ? "true" : "false";
    auto arr = [&](const char* key,
                   const std::vector<std::wstring>& v) {
        j += ",\"";
        j += key;
        j += "\":[";
        bool first = true;
        for (auto& f : v) {
            if (!first)
                j += ",";
            first = false;
            j += "\"";
            j += ToUtf8(f);
            j += "\"";
        }
        j += "]";
    };
    arr("core", coreFiles);
    arr("ai", aiFiles);
    j += "}\n";
    if (!WriteAllText(installPath + L"\\installed-files.json", j))
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

HRESULT ReadInstalledFiles(const std::wstring& installPath,
                           std::vector<std::wstring>& coreFiles,
                           std::vector<std::wstring>& aiFiles,
                           bool& aiInstalled) {
    coreFiles.clear();
    aiFiles.clear();
    const std::wstring text =
        ReadAllText(installPath + L"\\installed-files.json");
    if (text.empty())
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    aiInstalled =
        wcsstr(text.c_str(), L"\"aiInstalled\":true") != nullptr;
    auto arr = [&](const wchar_t* key, std::vector<std::wstring>& v) {
        std::wstring k = L"\"";
        k += key;
        k += L"\":[";
        const wchar_t* p = wcsstr(text.c_str(), k.c_str());
        if (!p)
            return;
        p += k.size();
        for (;;) {
            while (*p == L' ' || *p == L'\t' || *p == L'\r'
                   || *p == L'\n' || *p == L',')
                ++p;
            if (*p == L']' || *p == 0)
                break;
            if (*p != L'"')
                break;
            const wchar_t* e = wcschr(p + 1, L'"');
            if (!e)
                break;
            v.emplace_back(p + 1, e);
            p = e + 1;
        }
    };
    arr(L"core", coreFiles);
    arr(L"ai", aiFiles);
    if (coreFiles.empty())
        return HRESULT_FROM_WIN32(ERROR_FILE_CORRUPT);
    return S_OK;
}

HRESULT PackFileLists(std::vector<std::wstring>& coreFiles,
                      std::vector<std::wstring>& aiFiles) {
    coreFiles.clear();
    aiFiles.clear();
    const std::wstring text =
        ReadAllText(ExeDir() + L"\\payload-sizes.json");
    if (text.empty())
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    auto arr = [&](const wchar_t* key, std::vector<std::wstring>& v) {
        std::wstring k = L"\"";
        k += key;
        k += L"\":[";
        const wchar_t* p = wcsstr(text.c_str(), k.c_str());
        if (!p)
            return;
        p += k.size();
        for (;;) {
            while (*p == L' ' || *p == L'\t' || *p == L'\r'
                   || *p == L'\n' || *p == L',')
                ++p;
            if (*p == L']' || *p == 0)
                break;
            if (*p != L'"')
                break;
            const wchar_t* e = wcschr(p + 1, L'"');
            if (!e)
                break;
            v.emplace_back(p + 1, e);
            p = e + 1;
        }
    };
    arr(L"coreFiles", coreFiles);
    arr(L"aiFiles", aiFiles);
    if (coreFiles.empty())
        return HRESULT_FROM_WIN32(ERROR_FILE_CORRUPT);
    return S_OK;
}

bool PlayerRunning() {
    HANDLE snap =
        CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return false;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"LunarPlayer.exe") == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

HRESULT RemoveInstallation(const std::wstring& installPath,
                           const PhaseFn& phase,
                           volatile LONG* cancelFlag) {
    auto cancelled = [&]() {
        return cancelFlag
            && InterlockedCompareExchange(
                   const_cast<LONG*>(cancelFlag), 0, 0)
            != 0;
    };
    if (installPath.empty())
        return E_INVALIDARG;
    if (PlayerRunning())
        return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
    std::vector<std::wstring> core, ai;
    bool aiInstalled = false;
    HRESULT hr = ReadInstalledFiles(installPath, core, ai,
                                    aiInstalled);
    if (FAILED(hr))
        return hr; // no record: refuse rather than guess
    auto drop = [&](const std::vector<std::wstring>& files,
                    const wchar_t* ph) {
        size_t i = 0;
        for (auto& f : files) {
            if (cancelled()) {
                hr = HRESULT_FROM_WIN32(ERROR_CANCELLED);
                return false;
            }
            std::wstring p = installPath + L"\\" + f;
            // Pack entries use forward slashes; normalize.
            for (auto& c : p) {
                if (c == L'/')
                    c = L'\\';
            }
            if (phase)
                phase(ph, f.c_str());
            DeleteFileW(p.c_str()); // best-effort per file
            if (++i % 8 == 0 && phase) {
                wchar_t d[64]{};
                swprintf_s(d, L"%zu / %zu", i, files.size());
                phase(ph, d);
            }
        }
        return true;
    };
    if (!drop(core, L"Removing core files"))
        return hr;
    if (aiInstalled && !drop(ai, L"Removing AI files"))
        return hr;
    // Prune directories emptied by the removal (deepest first,
    // best-effort: non-empty dirs survive for user content). The pack
    // top dirs are included for pre-merge legacy layouts.
    static const wchar_t* kDirs[] = {
        L"imageformats", L"platforms", L"tls",
        L"core", L"ffmpeg", L"ai",
    };
    for (auto d : kDirs)
        RemoveDirectoryW((installPath + L"\\" + d).c_str());
    // Shortcuts (all-users locations used by the installer).
    wchar_t programs[MAX_PATH]{}, desktop[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_COMMON_PROGRAMS, nullptr, 0,
                     programs);
    SHGetFolderPathW(nullptr, CSIDL_COMMON_DESKTOPDIRECTORY, nullptr,
                     0, desktop);
    if (programs[0])
        DeleteFileW(
            (std::wstring(programs) + L"\\Lunar Player.lnk").c_str());
    if (desktop[0])
        DeleteFileW(
            (std::wstring(desktop) + L"\\Lunar Player.lnk").c_str());
    if (phase)
        phase(L"Removing registration", L"Windows Installed Apps");
    hr = UnregisterARP();
    if (FAILED(hr))
        return hr;
    DeleteFileW((installPath + L"\\install.json").c_str());
    DeleteFileW((installPath + L"\\installed-files.json").c_str());
    // The uninstaller copy cannot delete itself while running:
    // schedule it for next reboot, then drop the dir if empty.
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring me(self);
    std::wstring mine = UninstallerPath(installPath);
    for (auto& c : me)
        c = towlower(c);
    for (auto& c : mine)
        c = towlower(c);
    if (me == mine)
        MoveFileExW(self, nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    else
        DeleteFileW(mine.c_str());
    RemoveDirectoryW(installPath.c_str()); // succeeds only if empty
    if (phase)
        phase(L"Uninstall complete", L"");
    return S_OK;
}

std::wstring FindExtractor() {
    wchar_t env[1024]{};
    DWORD n = GetEnvironmentVariableW(L"LUNAR_SEVENZIP", env, 1024);
    if (n > 0 && n < 1024 && FileExists(env))
        return env;
    const std::wstring side = ExeDir() + L"\\7z.exe";
    if (FileExists(side))
        return side;
    const std::wstring pf = L"C:\\Program Files\\7-Zip\\7z.exe";
    if (FileExists(pf))
        return pf;
    return L"";
}

bool VcRedistPresent() {
    HKEY k = nullptr;
    LONG lr = RegOpenKeyExW(HKEY_LOCAL_MACHINE, kVcKey, 0,
                            KEY_QUERY_VALUE, &k);
    if (lr != ERROR_SUCCESS)
        return false;
    wchar_t v[64]{};
    DWORD n = sizeof(v), t = 0;
    lr = RegQueryValueExW(k, L"Version", nullptr, &t, (BYTE*)v,
                          &n);
    RegCloseKey(k);
    return lr == ERROR_SUCCESS && t == REG_SZ && n > sizeof(wchar_t);
}

HRESULT InstallVcRedist(const std::wstring& dlDir,
                        const PhaseFn& phase,
                        volatile LONG* cancelFlag) {
    if (VcRedistPresent())
        return S_OK;
    if (phase)
        phase(L"Preparing system components",
              L"Downloading VC++ runtime");
    if (!CreateDirectoryW(dlDir.c_str(), nullptr)
        && GetLastError() != ERROR_ALREADY_EXISTS)
        return HRESULT_FROM_WIN32(GetLastError());
    const std::wstring dst = dlDir + L"\\vc_redist.x64.exe";
    LONG ownedCancel = 0;
    volatile LONG* cf = cancelFlag ? cancelFlag : &ownedCancel;
    HRESULT hr = PackDownloader::Fetch(
        kVcRedistUrl, dst, 0, L"",
        [&](uint64_t rx, uint64_t total) {
            if (cancelFlag
                && InterlockedCompareExchange(
                       const_cast<LONG*>(cancelFlag), 0, 0)
                    != 0)
                return false;
            if (phase) {
                wchar_t d[64]{};
                swprintf_s(d, L"%llu MB", rx / (1024ULL * 1024ULL));
                phase(L"Preparing system components", d);
            }
            (void)total;
            return true;
        },
        cf);
    if (FAILED(hr))
        return hr;
    if (phase)
        phase(L"Preparing system components",
              L"Installing VC++ runtime");
    std::wstring cmd = L"\"" + dst
        + L"\" /install /quiet /norestart";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                        0, nullptr, nullptr, &si, &pi))
        return HRESULT_FROM_WIN32(GetLastError());
    const DWORD wr = WaitForSingleObject(pi.hProcess, 10 * 60 * 1000);
    DWORD ec = 0;
    if (wr == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    } else if (!GetExitCodeProcess(pi.hProcess, &ec)) {
        hr = HRESULT_FROM_WIN32(GetLastError());
    } else if (ec != 0 && ec != 1641 && ec != 3010) {
        hr = HRESULT_FROM_WIN32(ec);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return hr;
}

} // namespace Maintenance
