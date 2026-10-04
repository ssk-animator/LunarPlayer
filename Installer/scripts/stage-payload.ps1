# Lunar Player production staging (Phase: real packaging).
# Copies the shippable payload from build_x64 into a clean staging tree
# per the exclusion manifest. NOTHING is modified in build_x64.
# Output: Installer/staging/<version>/{core,ffmpeg,ai} + sizes.json
# Usage: stage-payload.ps1 [-Version 0.1.0-alpha]

param([string]$Version = '0.1.0-alpha')

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build = Join-Path $root 'build_x64'
$stage = Join-Path $root "Installer/staging/$Version"
if (-not (Test-Path -LiteralPath (Join-Path $build 'LunarPlayer.exe'))) {
    throw 'build_x64/LunarPlayer.exe missing - build Release first'
}
Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
$core = Join-Path $stage 'core'
$ffmpeg = Join-Path $stage 'ffmpeg'
$ai = Join-Path $stage 'ai'
New-Item -ItemType Directory -Path $core, $ffmpeg, $ai -Force | Out-Null

function Copy-Ship {
    param([string]$name, [string]$dest)
    $src = Join-Path $build $name
    if (-not (Test-Path -LiteralPath $src)) { throw "missing payload file: $name" }
    Copy-Item -LiteralPath $src -Destination (Join-Path $dest (Split-Path $name -Leaf)) -Force
}
function Copy-Tree {
    param([string]$name, [string]$dest)
    $src = Join-Path $build $name
    if (-not (Test-Path -LiteralPath $src)) { throw "missing payload dir: $name" }
    Copy-Item -LiteralPath $src -Destination (Join-Path $dest $name) -Recurse -Force
}

# ---- core: player + updater + Qt (no Test) + plugins ----
Copy-Ship 'LunarPlayer.exe' $core
$updaterExe = Join-Path $root 'Installer/updater/build_x64/Release/LunarPlayerUpdater.exe'
if (-not (Test-Path -LiteralPath $updaterExe)) { throw 'build the updater (Release) first' }
Copy-Item -LiteralPath $updaterExe -Destination (Join-Path $core 'LunarPlayerUpdater.exe') -Force
foreach ($d in 'Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','Qt6OpenGL.dll',
    'Qt6OpenGLWidgets.dll','Qt6Network.dll') { Copy-Ship $d $core }
Copy-Tree 'platforms' $core
Copy-Tree 'imageformats' $core
Copy-Tree 'tls' $core
# qoffscreen (test-only platform plugin) must not ship:
Remove-Item -Force (Join-Path $core 'platforms\qoffscreen.dll') -ErrorAction SilentlyContinue

# ---- ffmpeg / media engine ----
foreach ($d in 'avcodec-62.dll','avformat-62.dll','avutil-60.dll',
    'swresample-6.dll','swscale-9.dll','avfilter-11.dll') { Copy-Ship $d $ffmpeg }

# ---- ai subtitle studio runtime (CUDA chain proven required by dumpbin) ----
foreach ($d in 'whisper.dll','ggml.dll','ggml-base.dll','ggml-cpu.dll',
    'ggml-cuda.dll','cudart64_12.dll','cublas64_12.dll','cublasLt64_12.dll') {
    Copy-Ship $d $ai
}
# NOTE: Models/V5 (2.9 GB) and Models/Whisper/* (3.9 GB, CLI-test-only) are
# NEVER staged — models are download-on-demand by design.

# ---- exclusion audit: fail if a must-not-ship artifact sneaked in ----
$bad = Get-ChildItem $stage -Recurse -File | Where-Object {
    $_.Name -match 'Test\.dll$|\.exp$|\.lib$|\.pdb$|\.ilk$' -or
    $_.Name -match '^(LunarPlayer.*Test|sample_|LunarBench|LunarPlayerV5GateCli|LunarPlayerQATest)' -or
    $_.Name -eq 'qoffscreen.dll' }
if ($bad) { throw ("must-not-ship files staged: " + ($bad.Name -join ', ')) }

function DirBytes([string]$p) {
    (Get-ChildItem $p -Recurse -File | Measure-Object -Property Length -Sum).Sum
}
$sizes = @{
    version = $Version
    core = DirBytes $core
    ffmpeg = DirBytes $ffmpeg
    ai = DirBytes $ai
}
$sizes | ConvertTo-Json | Out-File -Encoding utf8 (Join-Path $stage 'sizes.json')
$total = $sizes.core + $sizes.ffmpeg + $sizes.ai
Write-Output ("STAGED {0}: core={1}MB ffmpeg={2}MB ai={3}MB total={4}MB" -f $Version,
    [math]::Round($sizes.core/1MB,1), [math]::Round($sizes.ffmpeg/1MB,1),
    [math]::Round($sizes.ai/1MB,1), [math]::Round($total/1MB,1))
