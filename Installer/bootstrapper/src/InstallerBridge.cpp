// InstallerBridge — narrow RPC implementation. All privileged operations
// funnel here with validation; React gets data, never handles.

#include "InstallerBridge.h"

#include "BundleVersion.h"
#include "LunarBA.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exdisp.h>
#include <shellapi.h>
#include <shldisp.h>
#include <shlobj.h>
#include <shlwapi.h>

namespace {
// Launches a process at the user's (medium) integrity via the Explorer
// shell automation object. Raw COM, no ATL: every acquired interface is
// released on all paths. True when Explorer accepted the launch.
bool LaunchDeelevated(const std::wstring& exe) {
    IShellWindows* psw = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr,
                                CLSCTX_LOCAL_SERVER,
                                IID_PPV_ARGS(&psw)))
        || !psw)
        return false;
    bool launched = false;
    IDispatch* pdDesktop = nullptr;
    VARIANT vLoc{}, vRoot{}, vEmpty{};
    long hwnd = 0;
    if (SUCCEEDED(psw->FindWindowSW(&vLoc, &vRoot, SWC_DESKTOP, &hwnd,
                                    SWFO_NEEDDISPATCH, &pdDesktop))
        && pdDesktop) {
        IServiceProvider* psp = nullptr;
        if (SUCCEEDED(pdDesktop->QueryInterface(IID_PPV_ARGS(&psp)))
            && psp) {
            IShellBrowser* pb = nullptr;
            if (SUCCEEDED(psp->QueryService(SID_STopLevelBrowser,
                                            IID_PPV_ARGS(&pb)))
                && pb) {
                IShellView* pv = nullptr;
                if (SUCCEEDED(pb->QueryActiveShellView(&pv)) && pv) {
                    IDispatch* pdView = nullptr;
                    if (SUCCEEDED(pv->GetItemObject(
                            SVGIO_BACKGROUND, IID_PPV_ARGS(&pdView)))
                        && pdView) {
                        IShellFolderViewDual* pfvd = nullptr;
                        if (SUCCEEDED(pdView->QueryInterface(
                                IID_PPV_ARGS(&pfvd)))
                            && pfvd) {
                            IDispatch* pdApp = nullptr;
                            if (SUCCEEDED(pfvd->get_Application(
                                    &pdApp))
                                && pdApp) {
                                IShellDispatch2* psh = nullptr;
                                if (SUCCEEDED(pdApp->QueryInterface(
                                        IID_PPV_ARGS(&psh)))
                                    && psh) {
                                    VARIANT vDir{}, vArgs{}, vShow{};
                                    vDir.vt = VT_BSTR;
                                    vDir.bstrVal = SysAllocString(
                                        L"");
                                    vShow.vt = VT_I4;
                                    vShow.lVal = SW_SHOWNORMAL;
                                    BSTR app = SysAllocString(
                                        exe.c_str());
                                    HRESULT hr = psh->ShellExecute(
                                        app, vArgs, vDir,
                                        vEmpty, vShow);
                                    SysFreeString(app);
                                    VariantClear(&vDir);
                                    launched = SUCCEEDED(hr);
                                    psh->Release();
                                }
                                pdApp->Release();
                            }
                            pfvd->Release();
                        }
                        pdView->Release();
                    }
                    pv->Release();
                }
                pb->Release();
            }
            psp->Release();
        }
        pdDesktop->Release();
    }
    psw->Release();
    return launched;
}
} // namespace

namespace {
// Minimal flat-JSON field extractor for the small, known-shape RPC args.
std::wstring JsonStringField(const std::wstring& json, const wchar_t* key) {
    std::wstring k = L"\"";
    k += key;
    k += L"\"";
    const wchar_t* p = wcsstr(json.c_str(), k.c_str());
    if (!p)
        return L"";
    const wchar_t* c = wcschr(p, L':');
    if (!c)
        return L"";
    const wchar_t* v1 = wcschr(c, L'"');
    if (!v1)
        return L"";
    const wchar_t* v2 = wcschr(v1 + 1, L'"');
    if (!v2)
        return L"";
    return std::wstring(v1 + 1, v2);
}

bool JsonBoolField(const std::wstring& json, const wchar_t* key,
                   size_t index) {
    // Extracts the index-th boolean from an "ids":[...] array.
    std::wstring k = L"\"";
    k += key;
    k += L"\"";
    const wchar_t* p = wcsstr(json.c_str(), k.c_str());
    if (!p)
        return false;
    const wchar_t* b = wcschr(p, L'[');
    if (!b)
        return false;
    size_t seen = 0;
    for (const wchar_t* q = b; *q && *q != L']'; ++q) {
        if (*q == L'"') {
            if (seen == index)
                return true;
            ++seen;
            q = wcschr(q + 1, L'"');
            if (!q)
                break;
        }
    }
    return false;
}

std::wstring JsonEscapeOut(const std::wstring& s) {
    std::wstring o;
    for (wchar_t c : s) {
        switch (c) {
        case L'"': o += L"\\\""; break;
        case L'\\': o += L"\\\\"; break;
        case L'\n': o += L"\\n"; break;
        case L'\r': o += L"\\r"; break;
        default: o += c;
        }
    }
    return o;
}

std::wstring DefaultInstallPath() {
    wchar_t* base = nullptr;
    std::wstring out = L"C:\\Program Files\\Lunar Player";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFilesX64, 0, nullptr,
                                       &base))) {
        out.assign(base);
        out += L"\\Lunar Player";
        CoTaskMemFree(base);
    }
    return out;
}

// Module dir of THIS dll (bundle cache in production; dev override via
// LUNAR_PAYLOAD_DIR for UI development). Uses GetModuleHandleExW with
// FROM_ADDRESS: GetModuleFileNameW needs a real HMODULE, never a raw
// code address cast (that fails and yields an empty root).
std::wstring PayloadRoot() {
    wchar_t env[32768]{};
    DWORD n = GetEnvironmentVariableW(L"LUNAR_PAYLOAD_DIR", env,
                                      (DWORD)(sizeof(env) / sizeof(env[0])));
    if (n > 0 && n < (DWORD)(sizeof(env) / sizeof(env[0])))
        return env;
    HMODULE hm = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                           | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&PayloadRoot), &hm);
    wchar_t mod[MAX_PATH]{};
    if (hm)
        GetModuleFileNameW(hm, mod, MAX_PATH);
    std::wstring d(mod);
    const size_t slash = d.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : d.substr(0, slash);
}
} // namespace

InstallerBridge::InstallerBridge(LunarBA* ba)
    : m_ba(ba)
    , m_installPath(DefaultInstallPath()) {
    m_selected = { L"core", L"ffmpeg", L"aiStudio" };
    // Local E2E test hook (same family as LUNAR_PAYLOAD_DIR):
    // redirect the install target to a writable dir so the external
    // flow can be proven without elevation.
    wchar_t dir[32768]{};
    DWORD n = GetEnvironmentVariableW(L"LUNAR_INSTALL_DIR", dir,
                                      (DWORD)(sizeof(dir) / sizeof(dir[0])));
    if (n > 0 && n < (DWORD)(sizeof(dir) / sizeof(dir[0])))
        m_installPath.assign(dir);
}

std::vector<BridgeComponent> InstallerBridge::Components() {
    return ProbeComponents();
}

DownloadPack InstallerBridge::CorePack() {
    // Ensure the manifest was parsed (ProbeComponents caches packs).
    ProbeComponents();
    return m_packs[0];
}

DownloadPack InstallerBridge::AiPack() {
    ProbeComponents();
    return m_packs[1];
}

uint64_t InstallerBridge::SelectedBytes() {
    uint64_t total = 0;
    for (const auto& c : ProbeComponents()) {
        if (c.selected)
            total += c.sizeBytes;
    }
    return total;
}

uint64_t InstallerBridge::AvailableBytes() {
    ULARGE_INTEGER freeBytes{};
    if (GetDiskFreeSpaceExW(m_installPath.c_str(), &freeBytes, nullptr,
                            nullptr))
        return freeBytes.QuadPart;
    // Install dir may not exist yet: fall back to the volume root so
    // the UI never shows a bogus zero (e.g. fresh C:\Program Files\...).
    wchar_t root[MAX_PATH]{};
    if (GetVolumePathNameW(m_installPath.c_str(), root, MAX_PATH)
        && GetDiskFreeSpaceExW(root, &freeBytes, nullptr, nullptr))
        return freeBytes.QuadPart;
    return 0;
}

std::wstring InstallerBridge::BundleVersion() {
    return LUNAR_BUNDLE_VERSION;
}

void InstallerBridge::SetOptionalSelected(const std::wstring& id,
                                          bool selected) {
    if (id != L"aiStudio" && id != L"aiModels")
        return; // required components stay selected
    m_selected.erase(
        std::remove(m_selected.begin(), m_selected.end(), id),
        m_selected.end());
    if (selected)
        m_selected.push_back(id);
}

bool InstallerBridge::IsSelected(const std::wstring& id) const {
    return std::find(m_selected.begin(), m_selected.end(), id)
        != m_selected.end();
}

bool InstallerBridge::BrowseForFolder(HWND owner) {
    IFileOpenDialog* dlg = nullptr;
    bool changed = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                   CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&dlg)))) {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);
        IShellItem* start = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(m_installPath.c_str(),
                                                  nullptr,
                                                  IID_PPV_ARGS(&start)))) {
            dlg->SetFolder(start);
            start->Release();
        }
        if (SUCCEEDED(dlg->Show(owner))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item))) {
                LPWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,
                                                  &path))) {
                    m_installPath.assign(path);
                    changed = true;
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dlg->Release();
    }
    return changed;
}

bool InstallerBridge::StartInstallNative() {
    if (!m_ba || !m_ba->HasEngine())
        return false;
    m_ba->RequestInstall();
    return true;
}

void InstallerBridge::CancelInstallNative() {
    if (m_ba && m_ba->HasEngine())
        m_ba->RequestCancel();
}

bool InstallerBridge::LaunchPlayerNative(std::wstring& error) {    std::wstring exe = m_installPath + L"\\LunarPlayer.exe";
    DWORD attrs = GetFileAttributesW(exe.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        error = L"player not found at install location";
        return false;
    }
    // De-elevated launch through Explorer (medium integrity, the
    // installing user): the player must not inherit the installer's
    // admin token — it exits immediately when elevated. Falls back to
    // a direct launch when Explorer automation is unavailable.
    if (LaunchDeelevated(exe))
        return true;
    HINSTANCE rc = ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr,
                                 nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)rc <= 32) {
        error = L"could not launch player";
        return false;
    }
    return true;
}

std::wstring InstallerBridge::LogoPath() const {
    return PayloadRoot() + L"\\LunarPlayer.png";
}

uint64_t InstallerBridge::DirSizeBytes(const std::wstring& dir,
                                       const wchar_t* const* names,
                                       size_t count) {
    uint64_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        std::wstring p = dir + L"\\" + names[i];
        DWORD attrs = GetFileAttributesW(p.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES)
            continue;
        if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
            WIN32_FIND_DATAW fd{};
            HANDLE h =
                FindFirstFileW((p + L"\\*").c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE)
                continue;
            do {
                if (!wcscmp(fd.cFileName, L".")
                    || !wcscmp(fd.cFileName, L".."))
                    continue;
                std::wstring fp = p + L"\\" + fd.cFileName;
                DWORD a2 = GetFileAttributesW(fp.c_str());
                if (a2 != INVALID_FILE_ATTRIBUTES
                    && !(a2 & FILE_ATTRIBUTE_DIRECTORY)) {
                    LARGE_INTEGER sz{};
                    sz.LowPart = fd.nFileSizeLow;
                    sz.HighPart = (LONG)fd.nFileSizeHigh;
                    total += (uint64_t)sz.QuadPart;
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        } else {
            WIN32_FIND_DATAW fd{};
            HANDLE h = FindFirstFileW(p.c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER sz{};
                sz.LowPart = fd.nFileSizeLow;
                sz.HighPart = (LONG)fd.nFileSizeHigh;
                total += (uint64_t)sz.QuadPart;
                FindClose(h);
            }
        }
    }
    return total;
}

std::vector<BridgeComponent> InstallerBridge::ProbeComponents() {
    const std::wstring root = PayloadRoot();
    // Launcher model: the BA cache holds only the UI, so measuring the
    // DLL directory would report 0 for every component. The packager
    // drops payload-sizes.json (measured MSI/staged bytes at packaging
    // time) next to the BA; prefer it, fall back to local probing for
    // development (LUNAR_PAYLOAD_DIR).
    uint64_t fileSizes[4] = { 0, 0, 0, 0 };
    uint64_t dlSizes[4] = { 0, 0, 0, 0 };
    DownloadPack packs[2];
    bool haveFileSizes = false;
    {
        HANDLE h = CreateFileW((root + L"\\payload-sizes.json").c_str(),
                               GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                               nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            char buf[2048]{};
            DWORD n = 0;
            if (ReadFile(h, buf, (DWORD)sizeof(buf) - 1, &n, nullptr)
                && n > 0) {
                buf[n] = 0;
                // Exact-key scan: "\"key\":" (prefix match would confuse
                // "core" with "coreDl"/"corePack").
                auto num = [&](const char* key) -> uint64_t {
                    char pat[64]{};
                    strcpy_s(pat, "\"");
                    strcat_s(pat, key);
                    strcat_s(pat, "\":");
                    const char* p = strstr(buf, pat);
                    if (!p)
                        return 0;
                    return (uint64_t)_strtoui64(p + strlen(pat), nullptr,
                                               10);
                };
                auto str = [&](const char* key) -> std::wstring {
                    char pat[64]{};
                    strcpy_s(pat, "\"");
                    strcat_s(pat, key);
                    strcat_s(pat, "\":\"");
                    const char* p = strstr(buf, pat);
                    if (!p)
                        return L"";
                    p += strlen(pat);
                    const char* e = strchr(p, '"');
                    if (!e || e - p > 256)
                        return L"";
                    std::wstring o;
                    for (const char* q = p; q < e; ++q)
                        o += (wchar_t)(unsigned char)*q;
                    return o;
                };
                static const char* kKeys[4] = { "core", "ffmpeg",
                                                "aiStudio", "aiModels" };
                static const char* kDl[4] = { "coreDl", "ffmpegDl",
                                              "aiStudioDl", "aiModelsDl" };
                for (int i = 0; i < 4; ++i) {
                    fileSizes[i] = num(kKeys[i]);
                    dlSizes[i] = num(kDl[i]);
                }
                // Nested pack objects: scan inside the pack block.
                auto pack = [&](const char* pk, DownloadPack& out) {
                    char pat[64]{};
                    strcpy_s(pat, "\"");
                    strcat_s(pat, pk);
                    strcat_s(pat, "\":");
                    const char* b = strstr(buf, pat);
                    if (!b)
                        return;
                    const char* fe = strstr(b, "\"file\":\"");
                    const char* se = strstr(b, "\"size\":");
                    const char* he = strstr(b, "\"sha256\":\"");
                    // "url" is optional (older manifests lack it); only
                    // accept it inside THIS pack block.
                    const char* ue = strstr(b, "\"url\":\"");
                    const char* blk = strchr(b, '{');
                    const char* bend =
                        blk ? strchr(blk + 1, '}') : nullptr;
                    if (ue && (!bend || ue > bend))
                        ue = nullptr;
                    if (!fe || !se || !he)
                        return;
                    fe += 8;
                    const char* fee = strchr(fe, '"');
                    if (!fee || fee - fe > 128)
                        return;
                    for (const char* q = fe; q < fee; ++q)
                        out.file += (wchar_t)(unsigned char)*q;
                    out.size = (uint64_t)_strtoui64(se + 7, nullptr, 10);
                    he += 10;
                    const char* hee = strchr(he, '"');
                    if (!hee || hee - he > 128)
                        return;
                    for (const char* q = he; q < hee; ++q) {
                        wchar_t c = (wchar_t)(unsigned char)*q;
                        out.sha256 += (c >= L'A' && c <= L'F') ? (wchar_t)(c + 32)
                                                              : c;
                    }
                    if (ue) {
                        ue += 7;
                        const char* uee = strchr(ue, '"');
                        if (uee && uee - ue > 8 && uee - ue <= 512) {
                            for (const char* q = ue; q < uee; ++q)
                                out.url += (wchar_t)(unsigned char)*q;
                        }
                    }
                    out.present = !out.file.empty() && out.size > 0
                        && out.sha256.size() == 64;
                };
                pack("corePack", packs[0]);
                pack("aiPack", packs[1]);
                // Cache for CorePack()/AiPack().
                m_packs[0] = packs[0];
                m_packs[1] = packs[1];
                haveFileSizes = true;
            }
            CloseHandle(h);
        }
    }
    static const wchar_t* kCore[] = {
        L"LunarPlayer.exe", L"LunarPlayerUpdater.exe",
        L"Qt6Core.dll", L"Qt6Gui.dll", L"Qt6Widgets.dll",
        L"Qt6OpenGL.dll", L"Qt6OpenGLWidgets.dll", L"Qt6Network.dll",
        L"platforms", L"imageformats", L"tls", L"install.json",
    };
    static const wchar_t* kFfmpeg[] = {
        L"avcodec-62.dll", L"avformat-62.dll", L"avutil-60.dll",
        L"swresample-6.dll", L"swscale-9.dll", L"avfilter-11.dll",
    };
    static const wchar_t* kAi[] = {
        L"whisper.dll", L"ggml.dll", L"ggml-base.dll", L"ggml-cpu.dll",
        L"ggml-cuda.dll",
    };
    std::vector<BridgeComponent> out;
    BridgeComponent c;
    c.id = L"core";
    c.name = L"Lunar Player";
    c.description =
        L"Application files, libraries and core features";
    c.required = true;
    c.selected = true;
    c.sizeBytes = haveFileSizes ? fileSizes[0]
                               : DirSizeBytes(root, kCore, _countof(kCore));
    c.downloadBytes = haveFileSizes ? dlSizes[0] : 0;
    out.push_back(c);
    c = BridgeComponent{};
    c.id = L"ffmpeg";
    c.name = L"FFmpeg / Media Engine";
    c.description = L"Video, audio and image format support";
    c.required = true;
    c.selected = true;
    c.sizeBytes = haveFileSizes ? fileSizes[1]
                               : DirSizeBytes(root, kFfmpeg,
                                              _countof(kFfmpeg));
    c.downloadBytes = haveFileSizes ? dlSizes[1] : 0;
    out.push_back(c);
    c = BridgeComponent{};
    c.id = L"aiStudio";
    c.name = L"AI Subtitle Studio";
    c.description =
        L"Speech recognition, transcription and translation tools";
    c.required = false;
    c.selected = true;
    c.sizeBytes = haveFileSizes ? fileSizes[2]
                               : DirSizeBytes(root, kAi, _countof(kAi));
    c.downloadBytes = haveFileSizes ? dlSizes[2] : 0;
    out.push_back(c);
    c = BridgeComponent{};
    c.id = L"aiModels";
    c.name = L"AI Models (Download later)";
    c.description =
        L"Whisper Large-V2 snapshot + Silero VAD \u00B7 GPU transcription needs 4.5 GB free VRAM";
    c.required = false;
    c.selected = false;
    // Real measured snapshot size when the packager recorded it;
    // 0 keeps the row honest ("--") when unknown. Never bundled.
    c.sizeBytes = haveFileSizes ? fileSizes[3] : 0;
    c.downloadBytes = haveFileSizes ? dlSizes[3] : 0;
    out.push_back(c);
    // Apply persisted selection.
    for (auto& comp : out) {
        if (!comp.required) {
            comp.selected =
                std::find(m_selected.begin(), m_selected.end(), comp.id)
                != m_selected.end();
        }
    }
    return out;
}

std::wstring InstallerBridge::Dispatch(const std::wstring& method,
                                       const std::wstring& argsJson) {
    if (method == L"GetInstallationInfo")
        return GetInstallationInfo(argsJson);
    if (method == L"BrowseForFolder")
        return BrowseForFolder(argsJson);
    if (method == L"GetDiskSpace")
        return GetDiskSpace(argsJson);
    if (method == L"SelectComponents")
        return SelectComponents(argsJson);
    if (method == L"StartInstall")
        return StartInstall(argsJson);
    if (method == L"CancelInstall")
        return CancelInstall(argsJson);
    if (method == L"LaunchPlayer")
        return LaunchPlayer(argsJson);
    throw std::wstring(L"unknown bridge method");
}

static std::wstring MbLabel(uint64_t bytes) {
    wchar_t b[64]{};
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
        swprintf_s(b, L"%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    else
        swprintf_s(b, L"%llu MB", bytes / (1024ULL * 1024ULL));
    return b;
}

std::wstring InstallerBridge::GetInstallationInfo(
    const std::wstring& /*args*/) {
    auto comps = ProbeComponents();
    uint64_t required = 0, available = 0;
    for (const auto& c : comps) {
        if (c.selected)
            required += c.sizeBytes;
    }
    ULARGE_INTEGER freeBytes{};
    if (GetDiskFreeSpaceExW(m_installPath.c_str(), &freeBytes, nullptr,
                            nullptr))
        available = freeBytes.QuadPart;
    std::wstring j = L"{\"installPath\":\""
        + JsonEscapeOut(m_installPath) + L"\",\"components\":[";
    bool first = true;
    for (const auto& c : comps) {
        if (!first)
            j += L",";
        first = false;
        j += L"{\"id\":\"" + c.id + L"\",\"name\":\""
            + JsonEscapeOut(c.name) + L"\",\"description\":\""
            + JsonEscapeOut(c.description) + L"\",\"required\":"
            + (c.required ? L"true" : L"false") + L",\"selected\":"
            + (c.selected ? L"true" : L"false") + L",\"sizeBytes\":"
            + std::to_wstring(c.sizeBytes) + L",\"sizeLabel\":\""
            + MbLabel(c.sizeBytes) + L"\"}";
    }
    j += L"],\"requiredBytes\":" + std::to_wstring(required)
        + L",\"availableBytes\":" + std::to_wstring(available)
        + L",\"version\":\"" LUNAR_BUNDLE_VERSION L"\"}";
    return j;
}

std::wstring InstallerBridge::BrowseForFolder(const std::wstring& args) {
    std::wstring current = JsonStringField(args, L"path");
    if (current.empty())
        current = m_installPath;
    IFileOpenDialog* dlg = nullptr;
    std::wstring selected;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                   CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&dlg)))) {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);
        IShellItem* start = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(current.c_str(), nullptr,
                                                  IID_PPV_ARGS(&start)))) {
            dlg->SetFolder(start);
            start->Release();
        }
        if (SUCCEEDED(dlg->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item))) {
                LPWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,
                                                  &path))) {
                    selected.assign(path);
                    m_installPath = selected;
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dlg->Release();
    }
    if (selected.empty())
        return L"null";
    return L"\"" + JsonEscapeOut(selected) + L"\"";
}

std::wstring InstallerBridge::GetDiskSpace(const std::wstring& args) {
    std::wstring path = JsonStringField(args, L"path");
    if (path.empty())
        path = m_installPath;
    ULARGE_INTEGER freeBytes{};
    if (!GetDiskFreeSpaceExW(path.c_str(), &freeBytes, nullptr, nullptr))
        throw std::wstring(L"cannot query disk space");
    return std::to_wstring(freeBytes.QuadPart);
}

std::wstring InstallerBridge::SelectComponents(const std::wstring& args) {
    // args: {"ids":["core","ffmpeg",...]} — required ones stay selected.
    m_selected.clear();
    m_selected.push_back(L"core");
    m_selected.push_back(L"ffmpeg");
    static const wchar_t* kOptional[] = { L"aiStudio", L"aiModels" };
    for (const wchar_t* id : kOptional) {
        // Flat scan for the quoted id inside the ids array.
        std::wstring q = L"\"";
        q += id;
        q += L"\"";
        if (wcsstr(args.c_str(), q.c_str()))
            m_selected.push_back(id);
    }
    (void)JsonBoolField(args, L"ids", 0); // documented, unused
    return L"{}";
}

std::wstring InstallerBridge::StartInstall(const std::wstring& /*args*/) {
    if (!m_ba || !m_ba->HasEngine())
        throw std::wstring(L"no bootstrapper backend");
    m_ba->RequestInstall();
    return L"{}";
}

std::wstring InstallerBridge::CancelInstall(const std::wstring& /*args*/) {
    if (!m_ba || !m_ba->HasEngine())
        throw std::wstring(L"no bootstrapper backend");
    m_ba->RequestCancel();
    return L"{}";
}

std::wstring InstallerBridge::LaunchPlayer(const std::wstring& /*args*/) {
    std::wstring exe = m_installPath + L"\\LunarPlayer.exe";
    DWORD attrs = GetFileAttributesW(exe.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
        throw std::wstring(L"player not found at install location");
    HINSTANCE rc = ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr,
                                 nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)rc <= 32)
        throw std::wstring(L"could not launch player");
    return L"{}";
}
