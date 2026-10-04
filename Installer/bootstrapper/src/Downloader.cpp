// PackDownloader — WinHTTP streaming GET + file copy fallback with
// cooperative cancel, size enforcement, and BCrypt SHA-256 verify.

#include "Downloader.h"

#include <bcrypt.h>
#include <new>
#include <winhttp.h>

#ifndef CRYPT_E_HASH_MISMATCH
#define CRYPT_E_HASH_MISMATCH _HRESULT_TYPEDEF_(0x80091007)
#endif

namespace {
constexpr DWORD kChunk = 1 << 20; // 1 MiB streaming buffer

bool Cancelled(volatile LONG* flag) {
    return flag
        && InterlockedCompareExchange(const_cast<LONG*>(flag), 0, 0)
               != 0;
}
} // namespace

bool PackDownloader::IsHttp(const std::wstring& urlOrFile) {
    return _wcsnicmp(urlOrFile.c_str(), L"http://", 7) == 0
        || _wcsnicmp(urlOrFile.c_str(), L"https://", 8) == 0;
}

std::wstring PackDownloader::Sha256File(const std::wstring& path) {
    std::wstring out;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return out;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hh = nullptr;
    BYTE buf[65536]{};
    DWORD n = 0;
    bool ok = false;
    if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
            &alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))
        && BCRYPT_SUCCESS(
               BCryptCreateHash(alg, &hh, nullptr, 0, nullptr, 0, 0))) {
        ok = true;
        while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0) {
            if (!BCRYPT_SUCCESS(BCryptHashData(hh, buf, n, 0))) {
                ok = false;
                break;
            }
        }
        if (ok) {
            BYTE digest[32]{};
            if (BCRYPT_SUCCESS(BCryptFinishHash(hh, digest,
                                               sizeof(digest), 0))) {
                wchar_t hex[65]{};
                for (int i = 0; i < 32; ++i)
                    swprintf_s(hex + i * 2, 3, L"%02x", digest[i]);
                out.assign(hex);
            } else {
                ok = false;
            }
        }
        BCryptDestroyHash(hh);
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    CloseHandle(h);
    return ok ? out : L"";
}

HRESULT PackDownloader::VerifyAndHash(
    const std::wstring& destPath, uint64_t expectedSize,
    const std::wstring& expectedSha256) {
    if (expectedSize > 0) {
        LARGE_INTEGER sz{};
        HANDLE h = CreateFileW(destPath.c_str(), GENERIC_READ,
                               FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return HRESULT_FROM_WIN32(GetLastError());
        if (!GetFileSizeEx(h, &sz)) {
            HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
            CloseHandle(h);
            return hr;
        }
        CloseHandle(h);
        if ((uint64_t)sz.QuadPart != expectedSize)
            return HRESULT_FROM_WIN32(ERROR_FILE_CORRUPT);
    }
    if (!expectedSha256.empty()) {
        std::wstring got = Sha256File(destPath);
        if (got.empty())
            return E_FAIL;
        if (_wcsicmp(got.c_str(), expectedSha256.c_str()) != 0)
            return CRYPT_E_HASH_MISMATCH;
    }
    return S_OK;
}

HRESULT PackDownloader::FetchFile(const std::wstring& path,
                                  const std::wstring& destPath,
                                  uint64_t expectedSize,
                                  const std::wstring& expectedSha256,
                                  const ProgressFn& progress,
                                  volatile LONG* cancelFlag) {
    std::wstring src = path;
    if (_wcsnicmp(src.c_str(), L"file:///", 8) == 0)
        src = src.substr(8);
    else if (_wcsnicmp(src.c_str(), L"file://", 7) == 0)
        src = src.substr(7);
    HANDLE in = CreateFileW(src.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
    if (in == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());
    LARGE_INTEGER total{};
    if (!GetFileSizeEx(in, &total)) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        CloseHandle(in);
        return hr;
    }
    HANDLE out = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                             nullptr);
    if (out == INVALID_HANDLE_VALUE) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        CloseHandle(in);
        return hr;
    }
    BYTE* buf = new (std::nothrow) BYTE[kChunk];
    if (!buf) {
        CloseHandle(in);
        CloseHandle(out);
        return E_OUTOFMEMORY;
    }
    HRESULT hr = S_OK;
    uint64_t done = 0;
    for (;;) {
        if (Cancelled(cancelFlag)) {
            hr = E_ABORT;
            break;
        }
        DWORD n = 0;
        if (!ReadFile(in, buf, kChunk, &n, nullptr)) {
            hr = HRESULT_FROM_WIN32(GetLastError());
            break;
        }
        if (n == 0)
            break;
        DWORD w = 0;
        if (!WriteFile(out, buf, n, &w, nullptr) || w != n) {
            hr = HRESULT_FROM_WIN32(GetLastError());
            break;
        }
        done += n;
        if (progress && !progress(done, (uint64_t)total.QuadPart)) {
            hr = E_ABORT;
            break;
        }
    }
    delete[] buf;
    CloseHandle(in);
    CloseHandle(out);
    if (FAILED(hr)) {
        DeleteFileW(destPath.c_str());
        return hr;
    }
    return VerifyAndHash(destPath, expectedSize, expectedSha256);
}

HRESULT PackDownloader::FetchHttp(const std::wstring& url,
                                  const std::wstring& destPath,
                                  uint64_t expectedSize,
                                  const std::wstring& expectedSha256,
                                  const ProgressFn& progress,
                                  volatile LONG* cancelFlag) {
    // Redirect-following GET (301/302/303/307/308, absolute http(s)
    // Location only, max 5 hops): required for short links such as the
    // Microsoft aka.ms VC++ redist URL. Anything else fails honestly.
    std::wstring current = url;
    for (int hop = 0; hop < 6; ++hop) {
        if (Cancelled(cancelFlag))
            return E_ABORT;
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof(uc);
        wchar_t host[256]{}, path[2048]{};
        uc.lpszHostName = host;
        uc.dwHostNameLength = (DWORD)(sizeof(host) / sizeof(host[0]));
        uc.lpszUrlPath = path;
        uc.dwUrlPathLength = (DWORD)(sizeof(path) / sizeof(path[0]));
        if (!WinHttpCrackUrl(current.c_str(), 0, 0, &uc))
            return HRESULT_FROM_WIN32(GetLastError());
        HINTERNET sess = WinHttpOpen(L"LunarPlayerInstaller/0.1",
                                     WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
        if (!sess)
            return HRESULT_FROM_WIN32(GetLastError());
        // Bounded waits so shutdown/cancel can always join the worker.
        WinHttpSetTimeouts(sess, 15000, 15000, 30000, 60000);
        HRESULT hr = S_OK;
        HINTERNET conn = nullptr, req = nullptr;
        HANDLE out = INVALID_HANDLE_VALUE;
        bool redirected = false;
        std::wstring location;
        conn = WinHttpConnect(sess, host, uc.nPort, 0);
        if (!conn) {
            hr = HRESULT_FROM_WIN32(GetLastError());
        } else {
            DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS)
                ? WINHTTP_FLAG_SECURE
                : 0;
            req = WinHttpOpenRequest(conn, L"GET", path, nullptr,
                                     WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     flags);
            if (!req) {
                hr = HRESULT_FROM_WIN32(GetLastError());
            } else if (!WinHttpSendRequest(
                           req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
                hr = HRESULT_FROM_WIN32(GetLastError());
            } else if (!WinHttpReceiveResponse(req, nullptr)) {
                hr = HRESULT_FROM_WIN32(GetLastError());
            } else {
                DWORD status = 0, slen = sizeof(status);
                if (!WinHttpQueryHeaders(
                        req,
                        WINHTTP_QUERY_STATUS_CODE
                            | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &slen,
                        WINHTTP_NO_HEADER_INDEX)) {
                    hr = HRESULT_FROM_WIN32(GetLastError());
                } else if (status == 301 || status == 302
                           || status == 303 || status == 307
                           || status == 308) {
                    wchar_t loc[2048]{};
                    DWORD llen = sizeof(loc);
                    if (!WinHttpQueryHeaders(
                            req, WINHTTP_QUERY_LOCATION,
                            WINHTTP_HEADER_NAME_BY_INDEX, loc, &llen,
                            WINHTTP_NO_HEADER_INDEX)) {
                        hr = HRESULT_FROM_WIN32(
                            ERROR_FILE_NOT_FOUND);
                    } else if (_wcsnicmp(loc, L"http://", 7) != 0
                               && _wcsnicmp(loc, L"https://", 8)
                                       != 0) {
                        hr = HRESULT_FROM_WIN32(
                            ERROR_FILE_NOT_FOUND);
                    } else {
                        location.assign(loc);
                        redirected = true;
                    }
                } else if (status != 200) {
                    hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
                } else {
                out = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0,
                                  nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
                if (out == INVALID_HANDLE_VALUE) {
                    hr = HRESULT_FROM_WIN32(GetLastError());
                } else {
                    // Content-Length when advertised (progress totals).
                    uint64_t total = expectedSize;
                    {
                        wchar_t cl[32]{};
                        DWORD clen = sizeof(cl);
                        if (WinHttpQueryHeaders(
                                req, WINHTTP_QUERY_CONTENT_LENGTH,
                                WINHTTP_HEADER_NAME_BY_INDEX, cl, &clen,
                                WINHTTP_NO_HEADER_INDEX)) {
                            total = _wtoll(cl);
                        }
                    }
                    BYTE* buf = new (std::nothrow) BYTE[kChunk];
                    if (!buf) {
                        hr = E_OUTOFMEMORY;
                    } else {
                        uint64_t done = 0;
                        for (;;) {
                            if (Cancelled(cancelFlag)) {
                                hr = E_ABORT;
                                break;
                            }
                            DWORD avail = 0;
                            if (!WinHttpQueryDataAvailable(req, &avail)) {
                                hr = HRESULT_FROM_WIN32(GetLastError());
                                break;
                            }
                            if (avail == 0)
                                break;
                            DWORD got = 0;
                            DWORD want =
                                avail > kChunk ? kChunk : avail;
                            if (!WinHttpReadData(req, buf, want, &got)) {
                                hr = HRESULT_FROM_WIN32(GetLastError());
                                break;
                            }
                            if (got == 0)
                                break;
                            DWORD w = 0;
                            if (!WriteFile(out, buf, got, &w, nullptr)
                                || w != got) {
                                hr = HRESULT_FROM_WIN32(GetLastError());
                                break;
                            }
                            done += got;
                            if (progress && !progress(done, total)) {
                                hr = E_ABORT;
                                break;
                            }
                        }
                        delete[] buf;
                    }
                }
            }
        }
    }
    if (req) WinHttpCloseHandle(req);
    if (conn) WinHttpCloseHandle(conn);
    if (sess) WinHttpCloseHandle(sess);
    if (out != INVALID_HANDLE_VALUE)
        CloseHandle(out);
    if (redirected) {
        DeleteFileW(destPath.c_str());
        current.swap(location);
        continue; // next hop (loop cap enforced by for)
    }
    if (FAILED(hr)) {
        DeleteFileW(destPath.c_str());
        return hr;
    }
    return VerifyAndHash(destPath, expectedSize, expectedSha256);
    }
    return HRESULT_FROM_WIN32(ERROR_TOO_MANY_LINKS);
}

HRESULT PackDownloader::Fetch(const std::wstring& urlOrFile,
                              const std::wstring& destPath,
                              uint64_t expectedSize,
                              const std::wstring& expectedSha256,
                              const ProgressFn& progress,
                              volatile LONG* cancelFlag) {
    if (IsHttp(urlOrFile))
        return FetchHttp(urlOrFile, destPath, expectedSize,
                         expectedSha256, progress, cancelFlag);
    return FetchFile(urlOrFile, destPath, expectedSize, expectedSha256,
                     progress, cancelFlag);
}
