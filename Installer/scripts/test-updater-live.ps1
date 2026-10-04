# Updater live-fixture test (Phase: updater verification).
# Localhost HTTPS (self-signed CA trusted in CurrentUser\Root ONLY for the
# test, then removed) + signed fake bundle + real update.json. Exercises
# the genuine UpdaterBridge paths: manifest fetch, version compare,
# download, size + SHA-256 + Authenticode verification, tamper rejection.
# Usage: test-updater-live.ps1   (exit 0 = full positive path verified)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$work = Join-Path ([System.IO.Path]::GetTempPath()) 'LunarUpdaterLive'
Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
$htdocs = Join-Path $work 'htdocs'
$fixture = Join-Path $work 'fixture'
New-Item -ItemType Directory -Path $htdocs, $fixture -Force | Out-Null

try {
    # ---- 1. test CA: self-signed leaf trusted in CU\Root for the run ----
    $cert = New-SelfSignedCertificate -DnsName '127.0.0.1' -CertStoreLocation `
        'Cert:\CurrentUser\My' -KeyExportPolicy Exportable -KeyLength 2048 `
        -HashAlgorithm SHA256 -Type CodeSigningCert -KeyUsage DigitalSignature `
        -NotAfter (Get-Date).AddDays(2) `
        -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.1,1.3.6.1.5.5.7.3.3')
    $pfxPass = 'lunar-test-' + (Get-Random)
    $pfxPath = Join-Path $work 'test.pfx'
    Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password `
        (ConvertTo-SecureString $pfxPass -AsPlainText -Force) | Out-Null
    Export-Certificate -Cert $cert -FilePath (Join-Path $work 'test.cer') | Out-Null
    Import-Certificate -FilePath (Join-Path $work 'test.cer') `
        -CertStoreLocation 'Cert:\CurrentUser\Root' | Out-Null
    Write-Output 'CERT: test CA trusted in CurrentUser\Root (removed after)'

    # ---- 2. fake bundle: real signed exe ----
    $updaterExe = Join-Path $root 'Installer\updater\build_x64\Release\LunarPlayerUpdater.exe'
    if (-not (Test-Path -LiteralPath $updaterExe)) { throw 'build the updater first' }
    $bundle = Join-Path $htdocs 'fake-bundle.exe'
    Copy-Item -LiteralPath $updaterExe -Destination $bundle -Force
    $signtool = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe'
    & $signtool sign /f $pfxPath /p $pfxPass /fd sha256 $bundle 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'signtool failed' }
    Write-Output 'SIGNED: fake bundle carries the test signature'

    # ---- 3. update.json with REAL size + sha256 ----
    $size = (Get-Item $bundle).Length
    $sha = (Get-FileHash -LiteralPath $bundle -Algorithm SHA256).Hash.ToLower()
    $port = 18443
    $manifest = @"
{
  "schema": 1,
  "product": "Lunar Player",
  "channel": "stable",
  "architecture": "x64",
  "version": "1.1.0",
  "installer": {
    "file": "fake-bundle.exe",
    "url": "https://127.0.0.1:${port}/fake-bundle.exe",
    "size": ${size},
    "sha256": "${sha}"
  },
  "minimumUpdater": "1.0.0"
}
"@
    $manifest | Out-File -Encoding utf8 (Join-Path $htdocs 'update.json')

    # ---- 4. fixture install record (older version -> update triggers) ----
    $playerDir = Join-Path $fixture 'player'
    New-Item -ItemType Directory -Path $playerDir -Force | Out-Null
    @"
{
  "schema": 1,
  "product": "Lunar Player",
  "version": "0.1.0-alpha",
  "architecture": "x64",
  "installPath": "$($playerDir -replace '\\', '\\')",
  "channel": "stable",
  "components": { "core": true, "ffmpeg": true, "aiSubtitleStudio": false, "aiModels": false }
}
"@ | Out-File -Encoding utf8 (Join-Path $fixture 'install.json')

    # ---- 5. HTTPS server (PEM from PFX via cryptography lib) ----
    # NOTE: pip writes WARNINGs to stderr; judge by exit code, not stream.
    cmd /c "C:\Python314\python.exe -m pip install --quiet cryptography 2>&1"
    if ($LASTEXITCODE -ne 0) { throw 'cannot install cryptography lib' }
    $pemScript = @'
import sys
from cryptography.hazmat.primitives.serialization import pkcs12, Encoding, PrivateFormat, NoEncryption
pfx = open(sys.argv[1], 'rb').read()
pk = pkcs12.load_pkcs12(pfx, sys.argv[2].encode())
open(sys.argv[3], 'wb').write(pk.key.private_bytes(Encoding.PEM, PrivateFormat.TraditionalOpenSSL, NoEncryption()))
open(sys.argv[4], 'wb').write(pk.cert.certificate.public_bytes(Encoding.PEM))
'@
    $pemScript | Out-File -Encoding ascii (Join-Path $work 'pfx2pem.py')
    C:\Python314\python.exe (Join-Path $work 'pfx2pem.py') $pfxPath $pfxPass `
        (Join-Path $work 'key.pem') (Join-Path $work 'cert.pem')
    $serveScript = @'
import http.server, ssl, os
os.chdir(r"__HTDOCS__")
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(r"__CERT__", keyfile=r"__KEY__")
srv = http.server.HTTPServer(('127.0.0.1', __PORT__), http.server.SimpleHTTPRequestHandler)
srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
srv.serve_forever()
'@
    $serveScript = $serveScript -replace '__HTDOCS__', $htdocs
    $serveScript = $serveScript -replace '__CERT__', (Join-Path $work 'cert.pem')
    $serveScript = $serveScript -replace '__KEY__', (Join-Path $work 'key.pem')
    $serveScript = $serveScript -replace '__PORT__', "$port"
    $serveScript | Out-File -Encoding ascii (Join-Path $work 'serve.py')
    $server = Start-Process C:\Python314\python.exe `
        -ArgumentList "`"$(Join-Path $work 'serve.py')`"" -PassThru -WindowStyle Hidden
    Start-Sleep 3
    if ($server.HasExited) { throw 'HTTPS fixture server failed to start' }

    try {
        # ---- 6. run the REAL updater fixture flow ----
        $env:LUNAR_UPDATER_TEST_DIR = $fixture
        $env:LUNAR_UPDATE_URL = "https://127.0.0.1:${port}/update.json"
        & $updaterExe | Out-Null
        $rc = $LASTEXITCODE
        $resultFile = Join-Path $fixture 'fixture-result.txt'
        if (Test-Path -LiteralPath $resultFile) {
            Get-Content $resultFile | Select-Object -Last 12
        }
        if ($rc -ne 0) { throw 'updater fixture flow failed' }
        Write-Output 'UPDATER LIVE FIXTURE PASSED'
    } finally {
        Remove-Item Env:\LUNAR_UPDATER_TEST_DIR -ErrorAction SilentlyContinue
        Remove-Item Env:\LUNAR_UPDATE_URL -ErrorAction SilentlyContinue
        Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
    }
} finally {
    # ---- 7. ALWAYS untrust + clean up ----
    Get-ChildItem 'Cert:\CurrentUser\Root' -ErrorAction SilentlyContinue |
        Where-Object { $_.Subject -match 'CN=127\.0\.0\.1' } |
        Remove-Item -Force -ErrorAction SilentlyContinue
    Get-ChildItem 'Cert:\CurrentUser\My' -ErrorAction SilentlyContinue |
        Where-Object { $_.Subject -match 'CN=127\.0\.0\.1' } |
        Remove-Item -Force -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
    Write-Output 'CLEANUP: test trust removed, fixture deleted'
}
