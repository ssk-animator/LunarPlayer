# Lunar Player signing gate (Phase: signing pipeline).
# Signs production artifacts ONLY when credentials are provided via
# environment (LUNAR_CERT_THUMBPRINT or LUNAR_CERT_PFX + LUNAR_CERT_PASSWORD).
# Without them it reports SKIP (unsigned local builds are normal) and NEVER
# fabricates a signature.
# Usage: sign-release.ps1 <file> [<file>...]  (exit 0 = signed-or-skipped)

$ErrorActionPreference = 'Stop'
if ($args.Count -eq 0) { throw 'sign-release.ps1: no files given' }

$signtool = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe'
if (-not (Test-Path -LiteralPath $signtool)) {
    Write-Output 'SIGN SKIP: signtool not found'
    exit 0
}

$thumb = $env:LUNAR_CERT_THUMBPRINT
$pfx = $env:LUNAR_CERT_PFX
if ([string]::IsNullOrEmpty($thumb) -and [string]::IsNullOrEmpty($pfx)) {
    Write-Output 'SIGN SKIP: no certificate configured (LUNAR_CERT_THUMBPRINT/LUNAR_CERT_PFX unset)'
    exit 0
}

foreach ($f in $args) {
    if (-not (Test-Path -LiteralPath $f)) { throw "missing file: $f" }
    if (-not [string]::IsNullOrEmpty($thumb)) {
        & $signtool sign /sha1 $thumb /tr http://timestamp.digicert.com /td sha256 /fd sha256 $f
    } else {
        & $signtool sign /f $pfx /p $env:LUNAR_CERT_PASSWORD /tr http://timestamp.digicert.com /td sha256 /fd sha256 $f
    }
    if ($LASTEXITCODE -ne 0) { throw "signtool failed for $f" }
    & $signtool verify /pa $f
    if ($LASTEXITCODE -ne 0) { throw "signature verify failed for $f" }
    Write-Output "SIGNED+VERIFIED: $f"
}
