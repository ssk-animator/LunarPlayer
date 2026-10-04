# Lunar Player production packaging (Phase: real release pipeline).
# Standalone elevated installer: LunarPlayerInstaller.exe (x64,
# requireAdministrator, native Direct2D UI) + sidecars
# (payload-sizes.json, LunarPlayer.png, 7z.exe/7z.dll). Large payloads
# (MSIs, VC++ runtime) stay EXTERNAL with DownloadUrl + RemotePayload
# (size + SHA-256 enforced by Burn before execution) for the legacy
# Burn path; the shipped installer downloads GitHub 7z packs.
# Usage: package-release.ps1 [-Version 0.1.0-alpha] [-DownloadBase URL]
# Exit codes: 0 = release artifacts produced and verified.

param(
    [string]$Version = '0.1.0-alpha',
    [string]$DownloadBase = 'https://github.com/ssk-animator/LunarPlayer/releases/download/v0.1.0-alpha'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$wix = Join-Path $root 'Installer\thirdparty\wix314'
$releaseDir = Join-Path $root "Releases\$Version"
$obj = Join-Path ([System.IO.Path]::GetTempPath()) 'LunarPkgReal'

# WiX version binding is numeric-only: 0.1.0-alpha -> 0.1.0.0.
$wixVersion = '0.1.0.0'
if ($Version -match '^(\d+)\.(\d+)\.(\d+)') {
    $wixVersion = "$($Matches[1]).$($Matches[2]).$($Matches[3]).0"
}

# 1. Stage (exclusion manifest enforced inside).
& (Join-Path $root 'Installer\scripts\stage-payload.ps1') -Version $Version
$stage = Join-Path $root "Installer/staging/$Version"

# 2. Native installer UI: no WebView dist is bundled anymore. The only
# BA payload is payload-sizes.json (measured staged bytes for the
# component list). The React tree under Installer/ui is retained for
# reference/updater work but is NOT part of the installer bundle.
$uiDist = Join-Path $root 'Installer\ui\dist'

Remove-Item -Recurse -Force $obj -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $obj -Force | Out-Null

# 3. heat harvest (component groups map to MSI features).
$groups = @(
    @{ dir = 'core'; group = 'AppPayload'; var = 'StageCore' },
    @{ dir = 'ffmpeg'; group = 'FfmpegPayload'; var = 'StageFfmpeg' },
    @{ dir = 'ai'; group = 'AiPayload'; var = 'StageAi' }
)
$defines = @()
foreach ($g in $groups) {
    $gdir = Join-Path $stage $g.dir
    & "$wix\heat.exe" dir $gdir -cg $g.group -gg `
        -scom -sreg -sfrag -suid -dr INSTALLFOLDER -var ("var." + $g.var) `
        -out (Join-Path $obj "$($g.group).wxs")
    if ($LASTEXITCODE -ne 0) { throw "heat failed for $($g.dir)" }
    $defines += "-d$($g.var)=$gdir"
}

# 4. candle MSIs.
$env:LUNAR_VERSION = $wixVersion
# MSIs link straight into packages/ (the release payload dir) — no
# intermediate dir, no duplicates, no confusion.
$pkgDir = Join-Path $releaseDir 'packages'
New-Item -ItemType Directory -Path $pkgDir -Force | Out-Null
& "$wix\candle.exe" -arch x64 @defines -out "$obj\" `
    (Join-Path $obj 'AppPayload.wxs') (Join-Path $obj 'FfmpegPayload.wxs') `
    (Join-Path $root 'Installer\burn\LunarPlayerApp.wxs')
if ($LASTEXITCODE -ne 0) { throw 'candle (app MSI) failed' }
& "$wix\candle.exe" -arch x64 @defines -out "$obj\" `
    (Join-Path $obj 'AiPayload.wxs') `
    (Join-Path $root 'Installer\burn\LunarPlayerAI.wxs')
if ($LASTEXITCODE -ne 0) { throw 'candle (AI MSI) failed' }

# 5. light MSIs.
$appMsi = Join-Path $pkgDir 'LunarPlayer.msi'
$aiMsi = Join-Path $pkgDir 'LunarPlayerAI.msi'
& "$wix\light.exe" -out $appMsi (Join-Path $obj 'AppPayload.wixobj') `
    (Join-Path $obj 'FfmpegPayload.wixobj') `
    (Join-Path $obj 'LunarPlayerApp.wixobj')
if ($LASTEXITCODE -ne 0) { throw 'light (app MSI) failed' }
& "$wix\light.exe" -out $aiMsi (Join-Path $obj 'AiPayload.wixobj') `
    (Join-Path $obj 'LunarPlayerAI.wixobj')
if ($LASTEXITCODE -ne 0) { throw 'light (AI MSI) failed' }

# 6. Burn bundle.
# NOTE: the BA builds Win32 (x86 Burn engine in wix314 binaries); the
# directory is build_x86, NOT build_x64.
$baDll = Join-Path $root 'Installer\bootstrapper\build_x86\Release\LunarBA.dll'
if (-not (Test-Path -LiteralPath $baDll)) { throw 'build LunarBA (Release) first' }
$vcRedist = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\v143\vc_redist.x64.exe'
if (-not (Test-Path -LiteralPath $vcRedist)) { throw 'vc_redist.x64.exe missing' }
# UI payload fragment: native installer needs only payload-sizes.json
# at the BA root (measured staged bytes for the component list). No
# WebView dist is bundled.
function Get-DirBytes([string]$dir) {
    if (-not (Test-Path -LiteralPath $dir)) { return 0 }
    $sum = 0
    Get-ChildItem $dir -Recurse -File -ErrorAction SilentlyContinue |
        ForEach-Object { $sum += $_.Length }
    return $sum
}
$coreBytes = Get-DirBytes (Join-Path $stage 'core')
$ffmpegBytes = Get-DirBytes (Join-Path $stage 'ffmpeg')
$aiBytes = Get-DirBytes (Join-Path $stage 'ai')
$modelsBytes = Get-DirBytes (Join-Path $root 'build_x64\Models\V5')
# External download packs (Phase 2): per-component download attribution.
# Core pack covers core+ffmpeg (split proportionally); AI pack covers
# aiStudio; models are never packed. Missing manifest -> zeros (UI then
# shows installed sizes only).
$coreDl = 0; $ffmpegDl = 0; $aiDl = 0
$corePackJson = '"corePack":null'
$aiPackJson = '"aiPack":null'
$dlManifest = Join-Path $root "Releases\$Version\downloads\manifest.json"
$ghManifest = Join-Path $root "Releases\$Version\downloads\github-release.json"
$ghUrls = @{}
if (Test-Path -LiteralPath $ghManifest) {
    try {
        $gh = Get-Content $ghManifest -Raw | ConvertFrom-Json
        # Keys must match the pack filenames below.
        $ghUrls[$gh.core.filename] = $gh.core.url
        $ghUrls[$gh.ai.filename] = $gh.ai.url
    } catch {
        Write-Output 'WARNING: github release manifest unreadable; pack URLs omitted'
    }
}
if (Test-Path -LiteralPath $dlManifest) {
    try {
        $dm = Get-Content $dlManifest -Raw | ConvertFrom-Json
        foreach ($p in $dm.packages) {
            if ($p.filename -eq 'LunarPlayer-Core.7z') {
                $den = $coreBytes + $ffmpegBytes
                if ($den -gt 0) {
                    $coreDl = [uint64]([double]$p.compressedSize * $coreBytes / $den)
                    $ffmpegDl = [uint64]$p.compressedSize - $coreDl
                }
                $coreUrl = ''
                if ($ghUrls.ContainsKey('LunarPlayer-Core.7z')) {
                    $coreUrl = ',"url":"' + $ghUrls['LunarPlayer-Core.7z'] + '"'
                }
                $corePackJson = '"corePack":{"file":"LunarPlayer-Core.7z","size":' +
                    $p.compressedSize + ',"sha256":"' + $p.sha256 + '"' +
                    $coreUrl + '}'
            }
            if ($p.filename -eq 'LunarPlayer-AI.7z') {
                $aiDl = [uint64]$p.compressedSize
                $aiUrl = ''
                if ($ghUrls.ContainsKey('LunarPlayer-AI.7z')) {
                    $aiUrl = ',"url":"' + $ghUrls['LunarPlayer-AI.7z'] + '"'
                }
                $aiPackJson = '"aiPack":{"file":"LunarPlayer-AI.7z","size":' +
                    $p.compressedSize + ',"sha256":"' + $p.sha256 + '"' +
                    $aiUrl + '}'
            }
        }
        # Exact installed-file attribution (flattened relative paths for
        # the uninstall record + modify-prune). Tolerant of the quirky
        # tuple layout: collect any object carrying .filename/.files.
        $coreFiles = @()
        $aiFiles = @()
        $packObjs = foreach ($e in $dm.packages) {
            if ($e -is [string] -or $null -eq $e) { continue }
            if ($e.filename) { $e; continue }
            foreach ($x in $e) {
                if ($x -isnot [string] -and $null -ne $x -and $x.filename) { $x }
            }
        }
        foreach ($p in $packObjs) {
            foreach ($f in $p.files) {
                $rel = ($f.path -replace '\\', '/')
                if ($rel -match '^(core|ffmpeg)/(.+)$') {
                    $coreFiles += $Matches[2]
                } elseif ($rel -match '^ai/(.+)$') {
                    $aiFiles += $Matches[1]
                }
            }
        }
        $coreFilesJson = ($coreFiles | ForEach-Object { '"' + $_ + '"' }) -join ','
        $aiFilesJson = ($aiFiles | ForEach-Object { '"' + $_ + '"' }) -join ','
    } catch {
        Write-Output 'WARNING: downloads manifest unreadable; download sizes set to 0'
    }
}
if (-not $coreFilesJson) { $coreFilesJson = '' }
if (-not $aiFilesJson) { $aiFilesJson = '' }
$sizesJson = '{"core":' + $coreBytes +
    ',"ffmpeg":' + $ffmpegBytes +
    ',"aiStudio":' + $aiBytes +
    ',"aiModels":' + $modelsBytes +
    ',"coreDl":' + $coreDl +
    ',"ffmpegDl":' + $ffmpegDl +
    ',"aiStudioDl":' + $aiDl +
    ',"aiModelsDl":0,' +
    $corePackJson + ',' + $aiPackJson +
    ',"coreFiles":[' + $coreFilesJson + ']' +
    ',"aiFiles":[' + $aiFilesJson + ']}'
$sizesFile = Join-Path $obj 'payload-sizes.json'
$sizesJson | Out-File -Encoding ascii $sizesFile
# Title-bar logo (official LunarPlayer.png) ships as a BA payload next
# to the DLL; the bridge resolves it via the module directory.
$logoSrc = Join-Path $root 'assets\LunarPlayer.png'
if (-not (Test-Path -LiteralPath $logoSrc)) { throw 'logo asset missing' }
$uiPayloadWxs = @'
<?xml version="1.0" encoding="UTF-8"?>
<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi">
  <Fragment>
    <PayloadGroup Id="UIPayload">
      <Payload SourceFile="__SIZESFILE__" Name="payload-sizes.json" Compressed="yes" />
      <Payload SourceFile="__LOGOFILE__" Name="LunarPlayer.png" Compressed="yes" />
    </PayloadGroup>
  </Fragment>
</Wix>
'@
$uiPayloadWxs.Replace('__SIZESFILE__', $sizesFile).Replace(
    '__LOGOFILE__', $logoSrc) |
    Out-File -Encoding utf8 (Join-Path $obj 'UIPayload.wxs')
& "$wix\candle.exe" -arch x64 -out "$obj\" (Join-Path $obj 'UIPayload.wxs')
if ($LASTEXITCODE -ne 0) { throw 'candle (UI payload) failed' }
& "$wix\candle.exe" -arch x64 -ext "$wix\WixUtilExtension.dll" -ext "$wix\WixBalExtension.dll" `
    "-dBADir=$(Split-Path $baDll)" "-dUIDir=$uiDist" "-dMsiDir=$pkgDir" `
    "-dAssetsDir=$(Join-Path $root 'assets')" `
    "-dVcRedistUrl=$DownloadBase/$Version/packages/vc_redist.x64.exe" `
    "-dVcRedistSize=$((Get-Item $vcRedist).Length)" `
    "-dVcRedistHash=$((Get-FileHash -LiteralPath $vcRedist -Algorithm SHA256).Hash)" `
    "-dAppMsiUrl=$DownloadBase/$Version/packages/LunarPlayer.msi" `
    "-dAiMsiUrl=$DownloadBase/$Version/packages/LunarPlayerAI.msi" `
    -out "$obj\" `
    (Join-Path $root 'Installer\burn\LunarPlayer.wxs')
if ($LASTEXITCODE -ne 0) { throw 'candle (bundle) failed' }
$bundleName = 'LunarPlayerInstaller.exe'
# The shipped installer is the standalone elevated EXE (no Burn engine:
# Burn cannot start elevated by design). The Burn bundle still links to
# a temp path as a build-integrity check of the BA DLL + wixobjs.
$bundleExe = Join-Path $releaseDir $bundleName
$burnExe = Join-Path $obj 'LunarPlayerInstaller-burn.exe'
# NOTE: the wix314 binaries ship a 32-bit Burn engine, so the BA DLL is
# built Win32 (standard v3 practice: 32-bit engine + 64-bit packages).
& "$wix\light.exe" -out $burnExe (Join-Path $obj 'LunarPlayer.wixobj') `
    (Join-Path $obj 'UIPayload.wixobj') `
    -ext "$wix\WixUtilExtension.dll" -ext "$wix\WixBalExtension.dll" -b "$obj"
if ($LASTEXITCODE -ne 0) { throw 'light (bundle) failed' }

# 6b. Standalone installer build (x64, requireAdministrator via linker
# MANIFESTUAC, real icon + version resources). Reuses the native BA
# sources; Burn is not involved at runtime.
$standaloneSrc = Join-Path $root 'Installer\standalone'
$standaloneBuild = Join-Path $standaloneSrc 'build-x64'
$msbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild)) { throw 'MSBuild (VS2022) missing' }
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { throw 'cmake missing from PATH' }
& $cmake -S $standaloneSrc -B $standaloneBuild -G 'Visual Studio 17 2022' -A x64 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'cmake (standalone) failed' }
& $msbuild (Join-Path $standaloneBuild 'LunarPlayerInstaller.vcxproj') `
    /p:Configuration=Release /p:Platform=x64 /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'msbuild (standalone) failed' }
$standaloneExe = Join-Path $standaloneBuild 'Release\LunarPlayerInstaller.exe'
if (-not (Test-Path -LiteralPath $standaloneExe)) { throw 'standalone exe missing' }
Copy-Item -LiteralPath $standaloneExe -Destination $bundleExe -Force

# 6c. Sidecars next to the shipped installer: payload manifest (sizes +
# pack URLs/hashes + file lists), title-bar/tray logo, extractor + its
# license. The installer resolves all three from its own directory.
Copy-Item -LiteralPath $sizesFile -Destination (Join-Path $releaseDir 'payload-sizes.json') -Force
Copy-Item -LiteralPath $logoSrc -Destination (Join-Path $releaseDir 'LunarPlayer.png') -Force
$sevenZipDir = 'C:\Program Files\7-Zip'
foreach ($z in '7z.exe', '7z.dll', 'License.txt') {
    $zp = Join-Path $sevenZipDir $z
    if (-not (Test-Path -LiteralPath $zp)) { throw "7-Zip sidecar missing: $zp" }
    Copy-Item -LiteralPath $zp -Destination (Join-Path $releaseDir $z) -Force
}

# 6d. Verify the shipped manifest demands startup elevation (linker
# MANIFESTUAC) — the fixed single-elevated-process requirement.
$mtExe = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' `
    -Recurse -Filter mt.exe -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1
if (-not $mtExe) { throw 'mt.exe (Windows SDK) missing' }
$mtVerify = Join-Path $obj 'installer-verify.manifest'
& $mtExe.FullName "-inputresource:$bundleExe;#1" "-out:$mtVerify" |
    Out-Null
if ($LASTEXITCODE -ne 0) { throw 'mt.exe manifest readback failed' }
$verifyText = Get-Content $mtVerify -Raw
if ($verifyText -notmatch 'level="requireAdministrator"') {
    throw 'manifest verification failed: requireAdministrator absent'
}
Write-Output 'installer manifest: requireAdministrator verified (linker MANIFESTUAC)'

# 7. Verify + manifest + launcher-model layout.
foreach ($f in $appMsi, $aiMsi, $bundleExe) {
    if (-not (Test-Path -LiteralPath $f)) { throw "missing artifact: $f" }
    if ((Get-Item $f).Length -lt 102400) { throw "artifact implausibly small: $f" }
}
# wixpdb link databases are build intermediates, not release artifacts.
Get-ChildItem $releaseDir -Recurse -Filter *.wixpdb | Remove-Item -Force
# Launcher model: large payloads live under packages/ (the download
# source); the bundle EXE stays small. A stray root-level redist (used
# as Burn's local source hint) is removed: packages/ is the single
# payload home and DownloadUrl is the single acquisition path.
Remove-Item -Force (Join-Path $releaseDir 'vc_redist.x64.exe') `
    -ErrorAction SilentlyContinue
Copy-Item -LiteralPath $vcRedist -Destination (Join-Path $pkgDir 'vc_redist.x64.exe') -Force
$bundleExeName = Split-Path $bundleExe -Leaf
$files = @($bundleExe,
    (Join-Path $pkgDir 'LunarPlayer.msi'),
    (Join-Path $pkgDir 'LunarPlayerAI.msi'),
    (Join-Path $pkgDir 'vc_redist.x64.exe'))
$hashLines = foreach ($f in $files) {
    $h = (Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower()
    "$h  $(Split-Path $f -Leaf)"
}
$hashLines | Out-File -Encoding ascii (Join-Path $releaseDir 'SHA256SUMS.txt')
$bundleHash = ((Get-FileHash -LiteralPath $bundleExe -Algorithm SHA256).Hash).ToLower()
$bundleSize = (Get-Item $bundleExe).Length
$bundleMB = [math]::Round($bundleSize/1MB,1)
if ($bundleSize -ge 100*1024*1024) {
    throw "bundle too large for launcher model: $bundleMB MB (must be < 100 MB)"
}
# DownloadBase may already end with the version/tag segment (e.g. a
# GitHub .../download/v0.1.0-alpha base): avoid doubling it.
# GitHub tags carry a 'v' prefix (v0.1.0-alpha) while $Version does
# not: treat both as "base already versioned" to avoid doubling.
$installerUrl = if ($DownloadBase.EndsWith("/$Version") -or $DownloadBase.EndsWith("/v$Version")) {
    "$DownloadBase/$bundleExeName"
} else {
    "$DownloadBase/$Version/$bundleExeName"
}
$updateJson = @"
{
  "schema": 1,
  "product": "Lunar Player",
  "channel": "stable",
  "architecture": "x64",
  "version": "$Version",
  "installer": {
    "file": "$bundleExeName",
    "url": "$installerUrl",
    "size": $bundleSize,
    "sha256": "$bundleHash"
  },
  "minimumUpdater": "1.0.0"
}
"@
$updateJson | Out-File -Encoding utf8 (Join-Path $releaseDir 'update.json')
Write-Output "RELEASE PACKAGED: $releaseDir"
Write-Output "  bundle: $bundleExeName ($bundleMB MB)"
# No stray root MSIs: packages/ is the single payload home.
Remove-Item -Force (Join-Path $releaseDir 'LunarPlayer.msi'),
    (Join-Path $releaseDir 'LunarPlayerAI.msi') -ErrorAction SilentlyContinue
Get-ChildItem $releaseDir -Recurse -File | Select-Object FullName, Length
