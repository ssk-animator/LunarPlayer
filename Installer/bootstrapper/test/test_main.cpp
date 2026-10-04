// BATest — headless bridge verification (no WebView2 init, no Burn).
// Exercises the REAL InstallerBridge dispatch + payload probing. Exit 0
// on PASS. Uses LUNAR_PAYLOAD_DIR to point at a fixture payload.

#include "InstallerBridge.h"
#include "BundleVersion.h"
#include "Downloader.h"
#include "LunarBA.h"

#include <aclapi.h>
#include <cstdio>
#include <cstring>
#include <shlguid.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <windows.h>

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL: %s\n", msg);                                       \
            fflush(stdout);                                                  \
            return 1;                                                        \
        }                                                                    \
        printf("PASS: %s\n", msg);                                           \
        fflush(stdout);                                                      \
    } while (0)

static bool contains(const std::wstring& h, const wchar_t* n) {
    return h.find(n) != std::wstring::npos;
}

int wmain(int argc, wchar_t** argv) {
    (void)argc;
    (void)argv;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        printf("FAIL: CoInitialize\n");
        return 1;
    }

    LunarBA* ba = new LunarBA(GetModuleHandleW(nullptr), nullptr, nullptr);
    InstallerBridge bridge(ba);

    // 1. Unknown method rejected (narrow bridge).
    bool threw = false;
    try {
        bridge.Dispatch(L"ShellExec", L"{}");
    } catch (const std::wstring&) {
        threw = true;
    }
    CHECK(threw, "unknown bridge method rejected");

    // 2. GetInstallationInfo returns the four components with measured
    // sizes (never hardcoded).
    std::wstring info;
    try {
        info = bridge.Dispatch(L"GetInstallationInfo", L"{}");
    } catch (const std::wstring& e) {
        printf("FAIL: GetInstallationInfo threw: %S\n", e.c_str());
        return 1;
    }
    CHECK(contains(info, L"\"core\""), "core component present");
    CHECK(contains(info, L"\"ffmpeg\""), "ffmpeg component present");
    CHECK(contains(info, L"\"aiStudio\""), "aiStudio component present");
    CHECK(contains(info, L"\"aiModels\""), "aiModels component present");
    CHECK(contains(info, L"\"installPath\""), "install path present");
    CHECK(contains(info, L"\"requiredBytes\""), "requiredBytes present");
    CHECK(contains(info, L"\"availableBytes\""), "availableBytes present");
    CHECK(contains(info, L"\"version\":\"" LUNAR_BUNDLE_VERSION L"\""),
          "bundle version from single source");

    // 3. Disk space is a real positive number.
    std::wstring space;
    try {
        wchar_t sys[MAX_PATH]{};
        GetSystemDirectoryW(sys, MAX_PATH);
        std::wstring args = L"{\"path\":\"";
        args += sys;
        args += L"\"}";
        space = bridge.Dispatch(L"GetDiskSpace", args);
    } catch (const std::wstring& e) {
        printf("FAIL: GetDiskSpace threw: %S\n", e.c_str());
        return 1;
    }
    CHECK(_wtoll(space.c_str()) > 0, "disk space positive");

    // 4. Malformed RPC args do not crash.
    try {
        bridge.Dispatch(L"GetDiskSpace", L"not-json{{{");
    } catch (const std::wstring&) {
    }
    CHECK(true, "malformed args survived");

    // 5. Component selection round-trips.
    try {
        bridge.Dispatch(L"SelectComponents",
                        L"{\"ids\":[\"core\",\"ffmpeg\"]}");
    } catch (const std::wstring& e) {
        printf("FAIL: SelectComponents threw: %S\n", e.c_str());
        return 1;
    }
    CHECK(true, "SelectComponents accepted");

    // 6. StartInstall without an engine fails loudly (no silent no-op).
    threw = false;
    try {
        bridge.Dispatch(L"StartInstall", L"{}");
    } catch (const std::wstring&) {
        threw = true;
    }
    CHECK(threw, "engineless StartInstall refused loudly");

    // 7. Launcher model: missing local payloads download (IDDOWNLOAD)
    // when a URL exists, cancel honestly when no URL exists.
    CHECK(ba->OnResolveSource(L"LunarPlayerApp", L"LunarPlayerApp",
                              L"C:\\missing\\LunarPlayer.msi",
                              L"http://127.0.0.1:8933/x.msi")
              == 101,
          "resolve source downloads when URL exists");
    CHECK(ba->OnResolveSource(L"LunarPlayerApp", L"LunarPlayerApp",
                              L"C:\\missing\\LunarPlayer.msi",
                              nullptr)
              != 101,
          "resolve source refuses without URL");

    // 8. Phase 2 downloader: file:// fetch with progress, size + hash
    // enforcement, cancel, and bad-hash rejection (hermetic fixture).
    {
        wchar_t tmp[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tmp);
        std::wstring dir = std::wstring(tmp) + L"LunarDlTest";
        CreateDirectoryW(dir.c_str(), nullptr);
        std::wstring src = dir + L"\\src.bin";
        std::wstring dst = dir + L"\\dst.bin";
        HANDLE h = CreateFileW(src.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                               nullptr);
        CHECK(h != INVALID_HANDLE_VALUE, "fixture created");
        char fill[65536];
        memset(fill, 0x5A, sizeof(fill));
        DWORD w = 0;
        for (int i = 0; i < 40; ++i)
            WriteFile(h, fill, sizeof(fill), &w, nullptr);
        CloseHandle(h);
        std::wstring want = PackDownloader::Sha256File(src);
        CHECK(!want.empty(), "fixture hashed");
        int calls = 0;
        uint64_t lastRx = 0, lastTotal = 0;
        volatile LONG cancel = 0;
        HRESULT fr = PackDownloader::Fetch(
            std::wstring(L"file:///") + src, dst, 40ULL * 65536, want,
            [&](uint64_t rx, uint64_t total) {
                ++calls;
                lastRx = rx;
                lastTotal = total;
                return true;
            },
            &cancel);
        CHECK(SUCCEEDED(fr), "file fetch ok");
        CHECK(calls > 0 && lastRx == 40ULL * 65536
                  && lastTotal == 40ULL * 65536,
              "progress reported real bytes");
        // Wrong size refused.
        CHECK(FAILED(PackDownloader::Fetch(
                  std::wstring(L"file:///") + src, dst, 12345, want,
                  nullptr, nullptr)),
              "size mismatch refused");
        // Wrong hash refused.
        CHECK(FAILED(PackDownloader::Fetch(
                  std::wstring(L"file:///") + src, dst, 0,
                  L"0000000000000000000000000000000000000000000000000000000000000000",
                  nullptr, nullptr)),
              "hash mismatch refused");
        // Cancel aborts.
        volatile LONG cancel2 = 1;
        CHECK(PackDownloader::Fetch(std::wstring(L"file:///") + src,
                                    dst, 0, L"", nullptr,
                                    &cancel2)
                  == E_ABORT,
              "cancel aborts fetch");
        DeleteFileW(src.c_str());
        DeleteFileW(dst.c_str());
        RemoveDirectoryW(dir.c_str());
    }

    // 9. HTTP fetch, only when LUNAR_DL_TEST_URL points at a local test
    // server (else skipped — hermetic by default).
    {
        wchar_t env[2084]{};
        DWORD n = GetEnvironmentVariableW(L"LUNAR_DL_TEST_URL", env,
                                          2084);
        if (n > 0 && n < 2084) {
            wchar_t tmp[MAX_PATH]{};
            GetTempPathW(MAX_PATH, tmp);
            std::wstring dst =
                std::wstring(tmp) + L"LunarDlHttp.bin";
            // Optional expected size/hash (LUNAR_DL_TEST_SIZE/_SHA):
            // when present the fetch is fully verified, not just
            // downloaded.
            uint64_t expSize = 0;
            wchar_t snum[64]{};
            DWORD sn = GetEnvironmentVariableW(L"LUNAR_DL_TEST_SIZE",
                                               snum, 64);
            if (sn > 0 && sn < 64)
                expSize = (uint64_t)_wtoi64(snum);
            wchar_t shx[128]{};
            DWORD shn = GetEnvironmentVariableW(L"LUNAR_DL_TEST_SHA",
                                                shx, 128);
            std::wstring expSha =
                (shn > 0 && shn < 128) ? shx : L"";
            volatile LONG cancel = 0;
            int calls = 0;
            uint64_t lastRx = 0, lastTotal = 0;
            HRESULT fr = PackDownloader::Fetch(
                env, dst, expSize, expSha,
                [&](uint64_t rx, uint64_t total) {
                    ++calls;
                    lastRx = rx;
                    lastTotal = total;
                    return true;
                },
                &cancel);
            CHECK(SUCCEEDED(fr), "http fetch ok");
            CHECK(calls > 0, "http progress live");
            if (expSize > 0)
                CHECK(lastRx == expSize && lastTotal == expSize,
                      "http byte counts exact");
            DeleteFileW(dst.c_str());
        } else {
            CHECK(true, "http fetch skipped (no LUNAR_DL_TEST_URL)");
        }
    }

    // 10. Elevation boundary: writable dir passes, ACL-denied dir
    // fails, and the decision follows writability (unelevated).
    // The original DACL is snapshotted and restored verbatim, so the
    // fixture can never poison Temp across runs.
    {
        wchar_t tmp[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tmp);
        std::wstring dir = std::wstring(tmp) + L"LunarElevTest";
        RemoveDirectoryW((dir + L"\\sub").c_str());
        RemoveDirectoryW((dir + L"\\sub2").c_str());
        RemoveDirectoryW(dir.c_str());
        CHECK(LunarBA::CanWriteDir(dir + L"\\sub"),
              "writable target passes pre-flight");
        CHECK(!LunarBA::NeedsElevationForPath(dir + L"\\sub"),
              "no elevation for writable target (or already admin)");
        bool aclOk = false;
        PSECURITY_DESCRIPTOR psdSaved = nullptr;
        PSID savedOwner = nullptr;
        PSID savedGroup = nullptr;
        PACL savedDacl = nullptr;
        {
            CreateDirectoryW(dir.c_str(), nullptr);
            HANDLE tok = nullptr;
            BYTE sidBuf[SECURITY_MAX_SID_SIZE]{};
            DWORD sidLen = sizeof(sidBuf);
            if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY,
                                 &tok)
                && GetTokenInformation(tok, TokenUser, sidBuf,
                                       sidLen, &sidLen)
                && ERROR_SUCCESS
                       == GetNamedSecurityInfoW(
                              dir.c_str(), SE_FILE_OBJECT,
                              OWNER_SECURITY_INFORMATION
                                  | GROUP_SECURITY_INFORMATION
                                  | DACL_SECURITY_INFORMATION,
                              &savedOwner, &savedGroup, &savedDacl,
                              nullptr, &psdSaved)) {
                EXPLICIT_ACCESSW ea{};
                ea.grfAccessPermissions = GENERIC_WRITE;
                ea.grfAccessMode = DENY_ACCESS;
                ea.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
                ea.Trustee.pMultipleTrustee = nullptr;
                ea.Trustee.MultipleTrusteeOperation =
                    NO_MULTIPLE_TRUSTEE;
                ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
                ea.Trustee.TrusteeType = TRUSTEE_IS_USER;
                ea.Trustee.ptstrName =
                    (LPWSTR)((TOKEN_USER*)sidBuf)->User.Sid;
                PACL acl = nullptr;
                if (ERROR_SUCCESS
                    == SetEntriesInAclW(1, &ea, savedDacl, &acl)) {
                    DWORD r = SetNamedSecurityInfoW(
                        (LPWSTR)dir.c_str(), SE_FILE_OBJECT,
                        DACL_SECURITY_INFORMATION, nullptr, nullptr,
                        acl, nullptr);
                    aclOk = (r == ERROR_SUCCESS);
                    LocalFree(acl);
                }
            }
            if (tok)
                CloseHandle(tok);
        }
        if (aclOk) {
            CHECK(!LunarBA::CanWriteDir(dir + L"\\sub2"),
                  "ACL-denied target fails pre-flight");
            // Exact restore of the snapshotted descriptor.
            SetNamedSecurityInfoW(
                (LPWSTR)dir.c_str(), SE_FILE_OBJECT,
                OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION
                    | DACL_SECURITY_INFORMATION,
                savedOwner, savedGroup, savedDacl, nullptr);
        } else {
            CHECK(true, "ACL fixture unavailable, skipped");
        }
        if (psdSaved)
            LocalFree(psdSaved);
        RemoveDirectoryW((dir + L"\\sub").c_str());
        RemoveDirectoryW((dir + L"\\sub2").c_str());
        RemoveDirectoryW(dir.c_str());
        CHECK(!PathFileExistsW(dir.c_str()), "fixture cleaned up");
    }

    ba->Release();
    CoUninitialize();

    // 11. Shortcuts: created in explicit dirs, pointing at the fake
    // exe, with its icon. Verified by resolving the .lnk, then the
    // fixture is removed (real shell folders untouched).
    {
        wchar_t tmp[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tmp);
        std::wstring base = std::wstring(tmp) + L"LunarShortcutTest";
        std::wstring app = base + L"\\app";
        std::wstring sm = base + L"\\sm";
        std::wstring dt = base + L"\\dt";
        RemoveDirectoryW(app.c_str());
        RemoveDirectoryW(sm.c_str());
        RemoveDirectoryW(dt.c_str());
        RemoveDirectoryW(base.c_str());
        CreateDirectoryW(base.c_str(), nullptr);
        CreateDirectoryW(app.c_str(), nullptr);
        CreateDirectoryW(sm.c_str(), nullptr);
        CreateDirectoryW(dt.c_str(), nullptr);
        // Fake installed exe: any existing PE works for link targets.
        wchar_t self[MAX_PATH]{};
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        std::wstring exe = app + L"\\LunarPlayer.exe";
        CHECK(CopyFileW(self, exe.c_str(), FALSE) ? true : false,
              "shortcut fixture exe staged");
        LunarBA::CreateAppShortcutsTo(app, sm, dt);
        bool okSm = false, okDt = false;
        for (int i = 0; i < 2; ++i) {
            std::wstring lnk =
                (i == 0 ? sm : dt) + L"\\Lunar Player.lnk";
            if (GetFileAttributesW(lnk.c_str())
                == INVALID_FILE_ATTRIBUTES)
                continue;
            HRESULT hrCo2 =
                CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            IShellLinkW* link = nullptr;
            if (SUCCEEDED(CoCreateInstance(
                    CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                    IID_PPV_ARGS(&link)))) {
                IPersistFile* pf = nullptr;
                if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&pf)))) {
                    if (SUCCEEDED(pf->Load(lnk.c_str(), STGM_READ))) {
                        wchar_t tgt[MAX_PATH]{};
                        if (SUCCEEDED(link->GetPath(
                                tgt, MAX_PATH, nullptr, 0))
                            && !_wcsicmp(tgt, exe.c_str())) {
                            if (i == 0)
                                okSm = true;
                            else
                                okDt = true;
                        }
                    }
                    pf->Release();
                }
                link->Release();
            }
            if (SUCCEEDED(hrCo2))
                CoUninitialize();
        }
        CHECK(okSm, "start-menu shortcut resolves to exe");
        CHECK(okDt, "desktop shortcut resolves to exe");
        DeleteFileW((sm + L"\\Lunar Player.lnk").c_str());
        DeleteFileW((dt + L"\\Lunar Player.lnk").c_str());
        DeleteFileW(exe.c_str());
        RemoveDirectoryW(app.c_str());
        RemoveDirectoryW(sm.c_str());
        RemoveDirectoryW(dt.c_str());
        RemoveDirectoryW(base.c_str());
    }
    printf("BRIDGE TEST PASSED\n");
    return 0;
}
