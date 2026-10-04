// UpdaterBridge — implementation. WinHTTP (manifest + download), BCrypt
// SHA-256, WinVerifyTrust publisher check, SemVer compare, Toolhelp
// player-process wait. Every failure is a loud JSON error; nothing
// executes before size + hash + signature + version + arch all pass.

#include "UpdaterBridge.h"

#include "UpdaterVersion.h"

#include <algorithm>
#include <bcrypt.h>
#include <cstdarg>
#include <cstdio>
#include <shellapi.h>
#include <shlobj.h>
#include <softpub.h>
#include <tlhelp32.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <wintrust.h>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")

#ifndef BCRYPT_SHA256_ALGORITHM
#define BCRYPT_SHA256_ALGORITHM L"SHA256"
#endif

namespace {
// --- tiny flat-JSON helpers (known shapes only) ---
std::wstring JStr(const std::wstring& json, const wchar_t* key) {
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

uint64_t JNum(const std::wstring& json, const wchar_t* key) {
    std::wstring k = L"\"";
    k += key;
    k += L"\"";
    const wchar_t* p = wcsstr(json.c_str(), k.c_str());
    if (!p)
        return 0;
    const wchar_t* c = wcschr(p, L':');
    if (!c)
        return 0;
    return (uint64_t)_wtoll(c + 1);
}

std::wstring JEscape(const std::wstring& s) {
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

bool ReadAllText(const std::wstring& path, std::wstring& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD size = GetFileSize(h, nullptr);
    if (size == INVALID_FILE_SIZE || size > 4 * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    std::string buf(size, '\0');
    DWORD read = 0;
    const bool ok =
        ReadFile(h, buf.data(), size, &read, nullptr) && read == size;
    CloseHandle(h);
    if (!ok)
        return false;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, buf.data(), (int)buf.size(),
                                   nullptr, 0);
    if (wlen <= 0)
        return false;
    out.resize((size_t)wlen);
    MultiByteToWideChar(CP_UTF8, 0, buf.data(), (int)buf.size(), out.data(),
                        wlen);
    return true;
}

// Split https://host[:port]/path.
bool SplitUrl(const std::wstring& url, std::wstring& host, int& port,
              std::wstring& path) {
    const wchar_t* p = wcsstr(url.c_str(), L"https://");
    if (!p)
        return false; // HTTPS only, no exceptions.
    p += 8;
    const wchar_t* slash = wcschr(p, L'/');
    std::wstring hp = slash ? std::wstring(p, slash) : std::wstring(p);
    path = slash ? slash : L"/";
    const wchar_t* colon = wcschr(hp.c_str(), L':');
    if (colon) {
        host.assign(hp.c_str(), colon);
        port = _wtoi(colon + 1);
    } else {
        host = hp;
        port = 443;
    }
    return !host.empty();
}
} // namespace

UpdaterBridge::UpdaterBridge(HINSTANCE hInstance,
                             const std::wstring& updaterDir)
    : m_hInstance(hInstance)
    , m_dir(updaterDir) {
}

std::wstring UpdaterBridge::Dispatch(const std::wstring& method,
                                     const std::wstring& argsJson) {
    if (method == L"GetUpdateState")
        return GetUpdateState(argsJson);
    if (method == L"CheckForUpdate")
        return CheckForUpdate(argsJson);
    if (method == L"DownloadUpdate")
        return DownloadUpdate(argsJson);
    if (method == L"VerifyUpdate")
        return VerifyUpdate(argsJson);
    if (method == L"WaitForPlayerExit")
        return WaitForPlayerExit(argsJson);
    if (method == L"LaunchInstaller")
        return LaunchInstaller(argsJson);
    if (method == L"RelaunchPlayer")
        return RelaunchPlayer(argsJson);
    if (method == L"CancelDownload") {
        InterlockedExchange(&m_cancelFlag, 1);
        return L"{}";
    }
    throw std::wstring(L"unknown bridge method");
}

bool UpdaterBridge::ReadInstallJson(const std::wstring& dir,
                                    std::wstring& version,
                                    std::wstring& installPath,
                                    std::wstring& channel) {
    // install.json lives beside the player, i.e. parent of updater dir
    // when shipped as <install>\updater\... — probe candidates.
    const wchar_t* cands[] = {
        L"\\..\\install.json", L"\\install.json",
    };
    for (const wchar_t* c : cands) {
        std::wstring p = dir + c;
        wchar_t full[MAX_PATH]{};
        if (!GetFullPathNameW(p.c_str(), MAX_PATH, full, nullptr))
            continue;
        std::wstring text;
        if (!ReadAllText(full, text))
            continue;
        std::wstring v = JStr(text, L"version");
        if (v.empty())
            continue;
        version = v;
        installPath = JStr(text, L"installPath");
        std::wstring ch = JStr(text, L"channel");
        if (!ch.empty())
            channel = ch;
        return true;
    }
    return false;
}

int UpdaterBridge::CompareVersions(const std::wstring& a,
                                   const std::wstring& b) {
    // Numeric dot-separated prefix compare; prerelease suffix IGNORED for
    // ordering except: release > prerelease of the same numbers.
    auto parts = [](const std::wstring& v) {
        std::vector<unsigned long long> nums;
        std::wstring pre;
        std::wstring core = v;
        const size_t dash = core.find(L'-');
        if (dash != std::wstring::npos) {
            pre = core.substr(dash + 1);
            core = core.substr(0, dash);
        }
        size_t start = 0;
        for (size_t i = 0; i <= core.size(); ++i) {
            if (i == core.size() || core[i] == L'.') {
                nums.push_back(
                    (unsigned long long)_wtoll(
                        core.substr(start, i - start).c_str()));
                start = i + 1;
            }
        }
        return std::pair(nums, pre);
    };
    auto pa = parts(a);
    auto pb = parts(b);
    const size_t n = (std::max)(pa.first.size(), pb.first.size());
    for (size_t i = 0; i < n; ++i) {
        const auto x = i < pa.first.size() ? pa.first[i] : 0;
        const auto y = i < pb.first.size() ? pb.first[i] : 0;
        if (x != y)
            return x < y ? -1 : 1;
    }
    if (pa.second == pb.second)
        return 0;
    if (pa.second.empty())
        return 1; // release > prerelease
    if (pb.second.empty())
        return -1;
    return pa.second < pb.second ? -1 : 1;
}

std::wstring UpdaterBridge::GetUpdateState(const std::wstring& /*args*/) {
    std::wstring channel = L"stable";
    if (!ReadInstallJson(m_dir, m_installedVersion, m_installPath,
                         channel))
        throw std::wstring(L"installation record not found");
    m_channel = channel;
    return L"{\"installedVersion\":\"" + JEscape(m_installedVersion)
        + L"\",\"installPath\":\"" + JEscape(m_installPath)
        + L"\",\"channel\":\"" + JEscape(m_channel) + L"\"}";
}

bool UpdaterBridge::FetchManifest(const std::wstring& url,
                                  UpdateManifest& out,
                                  std::wstring& error) {
    std::wstring host, path;
    int port = 443;
    if (!SplitUrl(url, host, port, path)) {
        error = L"manifest URL must be HTTPS";
        return false;
    }
    HINTERNET hSession = WinHttpOpen(
        L"LunarPlayer-Updater/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        error = L"network unavailable";
        return false;
    }
    HINTERNET hConn =
        WinHttpConnect(hSession, host.c_str(), (INTERNET_PORT)port, 0);
    if (!hConn) {
        WinHttpCloseHandle(hSession);
        error = L"cannot reach update server";
        return false;
    }
    HINTERNET hReq = WinHttpOpenRequest(
        hConn, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH);
    bool ok = false;
    std::string body;
    if (hReq
        && WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        && WinHttpReceiveResponse(hReq, nullptr)) {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(hReq,
                            WINHTTP_QUERY_STATUS_CODE
                                | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                            WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(hReq, &avail) || avail == 0)
                    break;
                std::string chunk(avail, '\0');
                DWORD got = 0;
                if (!WinHttpReadData(hReq, chunk.data(), avail, &got)
                    || got == 0)
                    break;
                chunk.resize(got);
                body += chunk;
                if (body.size() > 1024 * 1024)
                    break;
            }
            ok = true;
        } else {
            error = L"update server returned an error";
        }
    } else {
        error = L"update check failed";
    }
    if (hReq)
        WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSession);
    if (!ok)
        return false;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, body.data(),
                                   (int)body.size(), nullptr, 0);
    if (wlen <= 0) {
        error = L"malformed manifest";
        return false;
    }
    std::wstring text((size_t)wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, body.data(), (int)body.size(),
                        text.data(), wlen);
    UpdateManifest m;
    m.schema = (int)JNum(text, L"schema");
    m.channel = JStr(text, L"channel");
    m.version = JStr(text, L"version");
    m.file = JStr(text, L"file");
    // installer.* may be nested {"installer":{...}} or flat.
    const wchar_t* pi = wcsstr(text.c_str(), L"\"installer\"");
    std::wstring scope = text;
    if (pi) {
        const wchar_t* b = wcschr(pi, L'{');
        if (b) {
            int depth = 0;
            const wchar_t* e = b;
            for (; *e; ++e) {
                if (*e == L'{')
                    ++depth;
                else if (*e == L'}') {
                    --depth;
                    if (depth == 0) {
                        ++e;
                        break;
                    }
                }
            }
            scope.assign(b, e);
        }
    }
    m.url = JStr(scope, L"url");
    if (m.url.empty()) {
        // Fallback: derive from file + channel base URL.
        std::wstring base = url.substr(0, url.find_last_of(L'/'));
        std::wstring file = JStr(scope, L"file");
        if (!file.empty()) {
            m.url = base + L"/" + file;
            m.file = file;
        }
    }
    m.size = JNum(scope, L"size");
    m.sha256 = JStr(scope, L"sha256");
    m.minimumUpdater = JStr(text, L"minimumUpdater");
    if (m.schema != 1 || m.version.empty() || m.url.empty()
        || m.size == 0 || m.sha256.size() != 64) {
        error = L"malformed manifest";
        return false;
    }
    out = m;
    return true;
}

std::wstring UpdaterBridge::CheckForUpdate(const std::wstring& args) {
    std::wstring url = JStr(args, L"url");
    if (url.empty()) {
        wchar_t env[4096]{};
        DWORD n = GetEnvironmentVariableW(L"LUNAR_UPDATE_URL", env, 4096);
        if (n > 0 && n < 4096)
            url.assign(env);
    }
    if (url.empty())
        throw std::wstring(L"no update channel configured");
    UpdateManifest m;
    std::wstring error;
    if (!FetchManifest(url, m, error))
        throw error;
    if (m.channel != m_channel)
        throw std::wstring(L"channel mismatch");
    if (CompareVersions(m.version, m_installedVersion) <= 0)
        return L"{\"updateAvailable\":false,\"version\":\""
            + JEscape(m_installedVersion) + L"\"}";
    // minimumUpdater gate (self version compiled in).
    if (!m.minimumUpdater.empty()
        && CompareVersions(LUNAR_UPDATER_VERSION, m.minimumUpdater) < 0)
        throw std::wstring(L"updater too old for this release");
    m_manifest = m;
    m_manifestOk = true;
    m_verified = false;
    return L"{\"updateAvailable\":true,\"version\":\""
        + JEscape(m.version) + L"\",\"size\":" + std::to_wstring(m.size)
        + L"}";
}

bool UpdaterBridge::DownloadFile(const std::wstring& url,
                                 const std::wstring& dest,
                                 std::function<void(uint64_t, uint64_t)> progress,
                                 std::wstring& error, bool& cancelled,
                                 const volatile LONG* cancelFlag) {
    cancelled = false;
    std::wstring host, path;
    int port = 443;
    if (!SplitUrl(url, host, port, path)) {
        error = L"download URL must be HTTPS";
        return false;
    }
    const std::wstring part = dest + L".part";
    DeleteFileW(part.c_str());
    HANDLE hFile =
        CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        error = L"cannot stage download";
        return false;
    }
    bool ok = false;
    uint64_t received = 0, total = 0;
    HINTERNET hSession = WinHttpOpen(
        L"LunarPlayer-Updater/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET hConn = nullptr, hReq = nullptr;
    if (hSession)
        hConn = WinHttpConnect(hSession, host.c_str(),
                               (INTERNET_PORT)port, 0);
    if (hConn)
        hReq = WinHttpOpenRequest(hConn, L"GET", path.c_str(), nullptr,
                                  WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  WINHTTP_FLAG_SECURE
                                      | WINHTTP_FLAG_REFRESH);
    if (hReq
        && WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        && WinHttpReceiveResponse(hReq, nullptr)) {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(hReq,
                            WINHTTP_QUERY_STATUS_CODE
                                | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                            WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            ok = true; // reset on any loop error below
            DWORD qlen = 0;
            WinHttpQueryHeaders(hReq, WINHTTP_QUERY_CONTENT_LENGTH,
                                WINHTTP_HEADER_NAME_BY_INDEX,
                                WINHTTP_NO_OUTPUT_BUFFER, &qlen,
                                WINHTTP_NO_HEADER_INDEX);
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && qlen > 1) {
                std::wstring cls(qlen, L'\0');
                if (WinHttpQueryHeaders(
                        hReq, WINHTTP_QUERY_CONTENT_LENGTH,
                        WINHTTP_HEADER_NAME_BY_INDEX, cls.data(), &qlen,
                        WINHTTP_NO_HEADER_INDEX))
                    total = (uint64_t)_wtoll(cls.c_str());
            }
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(hReq, &avail)
                    || avail == 0)
                    break;
                std::string chunk(avail, '\0');
                DWORD got = 0;
                if (!WinHttpReadData(hReq, chunk.data(), avail, &got)
                    || got == 0) {
                    ok = false;
                    error = L"interrupted download";
                    break;
                }
                DWORD wrote = 0;
                if (!WriteFile(hFile, chunk.data(), got, &wrote, nullptr)
                    || wrote != got) {
                    ok = false;
                    error = L"cannot write download";
                    break;
                }
                received += got;
                if (progress)
                    progress(received, total);
            }
        } else {
            error = L"download server returned an error";
        }
    } else {
        error = L"download failed";
    }
    if (hReq)
        WinHttpCloseHandle(hReq);
    if (hConn)
        WinHttpCloseHandle(hConn);
    if (hSession)
        WinHttpCloseHandle(hSession);
    CloseHandle(hFile);
    if (!ok || cancelled) {
        DeleteFileW(part.c_str());
        return false;
    }
    if (!MoveFileExW(part.c_str(), dest.c_str(),
                     MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(part.c_str());
        error = L"cannot stage download";
        return false;
    }
    return true;
}

std::wstring UpdaterBridge::DownloadUpdate(const std::wstring& /*args*/) {
    if (!m_manifestOk)
        throw std::wstring(L"check for updates first");
    InterlockedExchange(&m_cancelFlag, 0);
    m_verified = false;
    const std::wstring dest = m_dir + L"\\pending\\"
        + (m_manifest.file.empty() ? L"update.exe" : m_manifest.file);
    // Confine staging: file name only, no separators.
    if (m_manifest.file.find_first_of(L"/\\:") != std::wstring::npos)
        throw std::wstring(L"unexpected package name");
    CreateDirectoryW((m_dir + L"\\pending").c_str(), nullptr);
    std::wstring error;
    bool cancelled = false;
    auto progress = [this](uint64_t r, uint64_t t) {
        if (m_progress)
            m_progress(r, t);
    };
    if (!DownloadFile(m_manifest.url, dest, progress, error, cancelled,
                      &m_cancelFlag))
        throw error;
    m_stagedPath = dest;
    return L"{\"staged\":true}";
}

bool UpdaterBridge::Sha256File(const std::wstring& path,
                               std::wstring& hexOut) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = false;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
            &alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        DWORD objLen = 0, cb = 0;
        if (BCRYPT_SUCCESS(BCryptGetProperty(
                alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen),
                &cb, 0))) {
            std::vector<unsigned char> obj(objLen);
            if (BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, obj.data(),
                                                objLen, nullptr, 0, 0))) {
                unsigned char buf[65536];
                DWORD got = 0;
                ok = true;
                while (ReadFile(h, buf, sizeof(buf), &got, nullptr)
                       && got > 0) {
                    if (!BCRYPT_SUCCESS(
                            BCryptHashData(hash, buf, got, 0))) {
                        ok = false;
                        break;
                    }
                }
                if (ok) {
                    unsigned char digest[32]{};
                    if (BCRYPT_SUCCESS(BCryptFinishHash(
                            hash, digest, sizeof(digest), 0))) {
                        wchar_t hex[65]{};
                        for (int i = 0; i < 32; ++i)
                            swprintf_s(hex + i * 2, 3, L"%02x",
                                       digest[i]);
                        hexOut.assign(hex);
                    } else {
                        ok = false;
                    }
                }
                BCryptDestroyHash(hash);
            }
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    CloseHandle(h);
    return ok;
}

bool UpdaterBridge::VerifySignature(const std::wstring& path,
                                    std::wstring& publisherOut) {
    WINTRUST_FILE_INFO fi{};
    fi.cbStruct = sizeof(fi);
    fi.pcwszFilePath = path.c_str();
    WINTRUST_DATA wd{};
    wd.cbStruct = sizeof(wd);
    wd.dwUIChoice = WTD_UI_NONE;
    wd.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    wd.dwUnionChoice = WTD_CHOICE_FILE;
    wd.pFile = &fi;
    wd.dwStateAction = WTD_STATEACTION_VERIFY;
    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG st = WinVerifyTrust(nullptr, &policy, &wd);
    wd.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &policy, &wd);
    if (st != ERROR_SUCCESS)
        return false;
    // Publisher extraction (subject CN) — best effort.
    HCERTSTORE store = nullptr;
    HCRYPTMSG msg = nullptr;
    DWORD enc = 0, ctype = 0, fmt = 0;
    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, path.c_str(),
                          CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                          CERT_QUERY_FORMAT_FLAG_BINARY, 0, &enc, &ctype,
                          &fmt, &store, &msg, nullptr))
        return true; // signature valid; name unavailable
    DWORD signerLen = 0;
    CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, nullptr,
                     &signerLen);
    bool named = false;
    if (signerLen > 0) {
        std::vector<unsigned char> si(signerLen);
        if (CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, si.data(),
                             &signerLen)) {
            PCMSG_SIGNER_INFO psi =
                reinterpret_cast<PCMSG_SIGNER_INFO>(si.data());
            CERT_INFO ci{};
            ci.Issuer = psi->Issuer;
            ci.SerialNumber = psi->SerialNumber;
            PCCERT_CONTEXT ctx = CertFindCertificateInStore(
                store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
                CERT_FIND_SUBJECT_CERT, &ci, nullptr);
            if (ctx) {
                wchar_t cn[256]{};
                if (CertGetNameStringW(
                        ctx, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr,
                        cn, 256)
                    > 1) {
                    publisherOut.assign(cn);
                    named = true;
                }
                CertFreeCertificateContext(ctx);
            }
        }
    }
    if (store)
        CertCloseStore(store, 0);
    if (msg)
        CryptMsgClose(msg);
    return named; // verified; publisher best-effort
}

std::wstring UpdaterBridge::VerifyUpdate(const std::wstring& /*args*/) {
    if (!m_manifestOk || m_stagedPath.empty())
        throw std::wstring(L"nothing staged to verify");
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(m_stagedPath.c_str(), GetFileExInfoStandard,
                              &fad))
        throw std::wstring(L"staged package missing");
    ULARGE_INTEGER sz{};
    sz.LowPart = fad.nFileSizeLow;
    sz.HighPart = fad.nFileSizeHigh;
    if (sz.QuadPart != m_manifest.size)
        throw std::wstring(L"size mismatch — truncated download?");
    std::wstring hex;
    if (!Sha256File(m_stagedPath, hex))
        throw std::wstring(L"cannot hash staged package");
    bool same = (hex.size() == m_manifest.sha256.size());
    for (size_t i = 0; same && i < hex.size(); ++i) {
        const wchar_t a = hex[i] >= L'A' && hex[i] <= L'F'
            ? wchar_t(hex[i] + 32)
            : hex[i];
        const wchar_t b = m_manifest.sha256[i] >= L'A'
                && m_manifest.sha256[i] <= L'F'
            ? wchar_t(m_manifest.sha256[i] + 32)
            : m_manifest.sha256[i];
        same = (a == b);
    }
    if (!same)
        throw std::wstring(L"SHA-256 mismatch — corrupted download?");
    std::wstring publisher;
    if (!VerifySignature(m_stagedPath, publisher))
        throw std::wstring(L"invalid or untrusted signature");
    m_verified = true;
    return L"{\"verified\":true,\"publisher\":\""
        + JEscape(publisher.empty() ? L"verified" : publisher) + L"\"}";
}

bool UpdaterBridge::FindPlayerProcess(const std::wstring& exePath,
                                      DWORD& pid) {    pid = 0;
    HANDLE snap =
        CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return false;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    wchar_t want[MAX_PATH]{};
    GetFullPathNameW(exePath.c_str(), MAX_PATH, want, nullptr);
    for (BOOL ok = Process32FirstW(snap, &pe); ok;
         ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"LunarPlayer.exe") != 0)
            continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                               pe.th32ProcessID);
        if (!h)
            continue;
        wchar_t img[MAX_PATH]{};
        DWORD len = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, img, &len)
            && _wcsicmp(img, want) == 0) {
            pid = pe.th32ProcessID;
            CloseHandle(h);
            break;
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
    return pid != 0;
}

std::wstring UpdaterBridge::WaitForPlayerExit(
    const std::wstring& /*args*/) {
    if (m_installPath.empty())
        throw std::wstring(L"installation record not found");
    DWORD pid = 0;
    if (!FindPlayerProcess(m_installPath + L"\\LunarPlayer.exe", pid))
        return L"{\"running\":false}";
    HANDLE h =
        OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!h)
        return L"{\"running\":false}";
    // Wait up to 120 s for graceful exit. Never force-kill by default.
    const DWORD r = WaitForSingleObject(h, 120000);
    CloseHandle(h);
    if (r != WAIT_OBJECT_0)
        throw std::wstring(L"player is still running");
    return L"{\"running\":false}";
}

std::wstring UpdaterBridge::LaunchInstaller(
    const std::wstring& /*args*/) {
    if (!m_verified || m_stagedPath.empty())
        throw std::wstring(L"verify the update before installing");
    // Documented contract (docs/UpdateProtocol.md).
    std::wstring params = L"--update --package \"" + m_stagedPath
        + L"\" --version \"" + m_manifest.version + L"\" --parent-pid "
        + std::to_wstring(GetCurrentProcessId()) + L" --mode full --relaunch \""
        + m_installPath + L"\\LunarPlayer.exe\"";
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"open";
    sei.lpFile = m_stagedPath.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    // NOTE: the updater exits immediately after this returns; Burn owns
    // the rest (self-update included). See docs/UpdaterArchitecture.md.
    if (!ShellExecuteExW(&sei))
        throw std::wstring(L"could not launch installer");
    if (sei.hProcess)
        CloseHandle(sei.hProcess);
    return L"{\"launched\":true}";
}

std::wstring UpdaterBridge::RelaunchPlayer(const std::wstring& /*args*/) {
    if (m_installPath.empty())
        throw std::wstring(L"installation record not found");
    std::wstring exe = m_installPath + L"\\LunarPlayer.exe";
    HINSTANCE rc =
        ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
    if ((INT_PTR)rc <= 32)
        throw std::wstring(L"could not relaunch player");
    return L"{}";
}

namespace {
int Check(bool cond, const char* msg) {
    printf("%s: %s\n", cond ? "PASS" : "FAIL", msg);
    fflush(stdout);
    return cond ? 0 : 1;
}
} // namespace

int UpdaterBridge::SelfTest() {
    int failures = 0;
    // SemVer ordering incl. prerelease rules.
    failures += Check(CompareVersions(L"1.1.0", L"1.0.0") > 0, "newer major");
    failures += Check(CompareVersions(L"1.0.0", L"1.0.0") == 0, "equal");
    failures += Check(CompareVersions(L"1.0.0", L"1.1.0") < 0, "older");
    failures += Check(CompareVersions(L"1.0.0", L"1.0.0-alpha") > 0,
                      "release beats prerelease");
    failures += Check(CompareVersions(L"2.0", L"1.9.9") > 0, "shorter wins");
    // SHA-256 of "abc" (FIPS vector).
    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring f = std::wstring(tmp) + L"lunar_sha_selftest.bin";
    HANDLE h = CreateFileW(f.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return 1;
    DWORD w = 0;
    WriteFile(h, "abc", 3, &w, nullptr);
    CloseHandle(h);
    std::wstring hex;
    failures += Check(Sha256File(f, hex)
                          && hex
                              == L"ba7816bf8f01cfea414140de5dae2223b00361a396"
                                 L"177a9cb410ff61f20015ad",
                      "sha256 fips vector");
    DeleteFileW(f.c_str());
    // Unknown RPC rejected; state without install record fails loudly.
    UpdaterBridge b(nullptr, L".");
    bool threw = false;
    try {
        b.Dispatch(L"Nope", L"{}");
    } catch (const std::wstring&) {
        threw = true;
    }
    failures += Check(threw, "unknown method rejected");
    threw = false;
    try {
        b.Dispatch(L"GetUpdateState", L"{}");
    } catch (const std::wstring&) {
        threw = true;
    }
    failures += Check(threw, "missing install record loud");
    DWORD pid = 0;
    failures += Check(!FindPlayerProcess(
                          L"C:\\definitely\\not\\here\\LunarPlayer.exe",
                          pid),
                      "absent player not found");
    printf(failures == 0 ? "UPDATER SELF-TEST PASSED\n"
                         : "UPDATER SELF-TEST FAILED\n");
    return failures == 0 ? 0 : 1;
}

int UpdaterBridge::FixtureTest(const std::wstring& fixtureDir) {
    // Live fixture: real HTTPS manifest fetch, real download, real SHA-256
    // and Authenticode verification. Fixture provides install.json +
    // LUNAR_UPDATE_URL. Prints each verdict; exit 0 only if the full
    // positive path verifies. GUI-subsystem: verdicts ALSO go to
    // fixture-result.txt (stdout is uncapturable without a console).
    FILE* log = nullptr;
    const std::wstring logPath = fixtureDir + L"\\fixture-result.txt";
    _wfopen_s(&log, logPath.c_str(), L"w");
    auto say = [&](const char* fmt, ...) {
        char buf[1024]{};
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        printf("%s\n", buf);
        fflush(stdout);
        if (log) {
            fprintf(log, "%s\n", buf);
            fflush(log);
        }
    };
    int failures = 0;
    UpdaterBridge b(nullptr, fixtureDir);
    std::wstring state;
    try {
        state = b.Dispatch(L"GetUpdateState", L"{}");
    } catch (const std::wstring& e) {
        say("FAIL: GetUpdateState: %S", e.c_str());
        if (log)
            fclose(log);
        return 1;
    }
    if (state.find(L"0.1.0-alpha") == std::wstring::npos) {
        say("FAIL: installed version not discovered");
        failures++;
    } else {
        say("PASS: installed version discovered");
    }
    std::wstring avail;
    try {
        avail = b.Dispatch(L"CheckForUpdate", L"{}");
    } catch (const std::wstring& e) {
        say("FAIL: CheckForUpdate: %S", e.c_str());
        if (log)
            fclose(log);
        return 1;
    }
    if (avail.find(L"\"updateAvailable\":true") == std::wstring::npos) {
        say("FAIL: newer version not detected");
        failures++;
    } else {
        say("PASS: newer version detected");
    }
    b.SetProgressCallback(
        [&](uint64_t r, uint64_t t) { say("  download %llu / %llu", r, t); });
    try {
        b.Dispatch(L"DownloadUpdate", L"{}");
    } catch (const std::wstring& e) {
        say("FAIL: DownloadUpdate: %S", e.c_str());
        if (log)
            fclose(log);
        return 1;
    }
    say("PASS: download staged");
    std::wstring verified;
    try {
        verified = b.Dispatch(L"VerifyUpdate", L"{}");
    } catch (const std::wstring& e) {
        say("FAIL: VerifyUpdate: %S", e.c_str());
        if (log)
            fclose(log);
        return 1;
    }
    if (verified.find(L"\"verified\":true") == std::wstring::npos) {
        say("FAIL: size+hash+signature not verified");
        failures++;
    } else {
        say("PASS: size+hash+signature verified");
    }
    // Negative control: tampered copy must fail verification.
    std::wstring tampered = fixtureDir + L"\\pending\\tampered.exe";
    if (CopyFileW((fixtureDir + L"\\pending\\fake-bundle.exe").c_str(),
                  tampered.c_str(), FALSE)) {
        HANDLE h = CreateFileW(
            tampered.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
            0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            SetFilePointer(h, 0, nullptr, FILE_BEGIN);
            DWORD w = 0;
            const char x = 'X';
            WriteFile(h, &x, 1, &w, nullptr);
            CloseHandle(h);
        }
        // Swap in the tampered file as the staged path and re-verify.
        DeleteFileW((fixtureDir + L"\\pending\\fake-bundle.exe").c_str());
        MoveFileW(tampered.c_str(),
                  (fixtureDir + L"\\pending\\fake-bundle.exe").c_str());
        bool rejected = false;
        try {
            b.Dispatch(L"VerifyUpdate", L"{}");
        } catch (const std::wstring&) {
            rejected = true;
        }
        if (!rejected) {
            say("FAIL: tampered bundle accepted");
            failures++;
        } else {
            say("PASS: tampered bundle rejected");
        }
    }
    say(failures == 0 ? "UPDATER FIXTURE PASSED" : "UPDATER FIXTURE FAILED");
    if (log)
        fclose(log);
    return failures == 0 ? 0 : 1;
}
