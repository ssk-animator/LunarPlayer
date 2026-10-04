#pragma once

// PackDownloader — Phase 2 external-package acquisition (local only).
// Transports: http(s) via WinHTTP, or file:// / plain paths via a
// streamed copy. Reports real byte progress; honors cooperative cancel;
// verifies size (when known) and SHA-256 before returning success.
// No Burn, no MSI, no Firebase.

#include <stdint.h>
#include <functional>
#include <string>
#include <windows.h>

class PackDownloader {
public:
    // Progress callback: received/total bytes. Return false to cancel.
    // May be invoked from the calling thread (synchronous Fetch).
    using ProgressFn = std::function<bool(uint64_t received,
                                          uint64_t total)>;

    // Fetch urlOrFile -> destPath (overwritten). expectedSize==0 skips
    // the size check; empty expectedSha256 skips hash check.
    // Returns S_OK, E_ABORT (cancelled), WININET/HTTP errors, or
    // CRYPT_E_HASH_MISMATCH / ERROR_FILE_CORRUPT class failures.
    static HRESULT Fetch(const std::wstring& urlOrFile,
                         const std::wstring& destPath,
                         uint64_t expectedSize,
                         const std::wstring& expectedSha256,
                         const ProgressFn& progress,
                         volatile LONG* cancelFlag);

    // SHA-256 of a file, lowercase hex. Empty on failure.
    static std::wstring Sha256File(const std::wstring& path);

    // True for http:// and https:// (otherwise treated as a file path,
    // with optional file:/// prefix).
    static bool IsHttp(const std::wstring& urlOrFile);

private:
    static HRESULT FetchHttp(const std::wstring& url,
                             const std::wstring& destPath,
                             uint64_t expectedSize,
                             const std::wstring& expectedSha256,
                             const ProgressFn& progress,
                             volatile LONG* cancelFlag);
    static HRESULT FetchFile(const std::wstring& path,
                             const std::wstring& destPath,
                             uint64_t expectedSize,
                             const std::wstring& expectedSha256,
                             const ProgressFn& progress,
                             volatile LONG* cancelFlag);
    static HRESULT VerifyAndHash(const std::wstring& destPath,
                                 uint64_t expectedSize,
                                 const std::wstring& expectedSha256);
};
