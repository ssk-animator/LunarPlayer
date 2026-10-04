# Lunar Player external download packages (Phase 1: local only).
# Builds ONE compressed Core pack (core+ffmpeg) and ONE AI pack (ai, no
# models) from the staged payload with benchmarked lossless compression.
# No Firebase, no uploader, no installer/UI changes.
# Usage: package-external.ps1 [-Version 0.1.0-alpha]
# Output: Releases/<version>/downloads/{LunarPlayer-Core.7z,
#         LunarPlayer-AI.7z,manifest.json,SHA256SUMS.txt}

param(
    [string]$Version = '0.1.0-alpha'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$stage = Join-Path $root "Installer\staging\$Version"
$outDir = Join-Path $root "Releases\$Version\downloads"
$work = Join-Path ([System.IO.Path]::GetTempPath()) 'LunarExtPkg'
$sevenZip = 'C:\Program Files\7-Zip\7z.exe'
if (-not (Test-Path -LiteralPath $sevenZip)) { throw '7-Zip missing' }
if (-not (Test-Path -LiteralPath (Join-Path $stage 'core'))) {
    throw "staging missing: $stage (run stage-payload.ps1 first)"
}

Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $work -Force | Out-Null
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

function Get-TreeBytes([string]$dir) {
    $s = 0
    Get-ChildItem $dir -Recurse -File | ForEach-Object { $s += $_.Length }
    return $s
}

# Deterministic file order: sorted relative paths via listfile.
function New-ListFile([string[]]$relPaths, [string]$listPath) {
    ($relPaths | Sort-Object) -join "`r`n" |
        Out-File -Encoding ascii -NoNewline $listPath
}

function Get-RelFiles([string]$dir) {
    Get-ChildItem $dir -Recurse -File |
        ForEach-Object {
            $_.FullName.Substring($dir.Length + 1)
        }
}

# Benchmark a directory set into one archive; returns the best result.
function Compress-Best([string]$baseDir, [string[]]$topDirs,
                       [string]$outBase) {
    $list = Join-Path $work 'files.lst'
    $rels = foreach ($d in $topDirs) {
        $full = Join-Path $baseDir $d
        foreach ($f in (Get-RelFiles $full)) { Join-Path $d $f }
    }
    New-ListFile $rels $list
    $cands = @(
        @{ tag = 'lzma2-9'; args = @('a', '-t7z', '-m0=lzma2', '-mx=9',
                '-ms=on', '-mmt=on') },
        @{ tag = 'lzma2-5'; args = @('a', '-t7z', '-m0=lzma2', '-mx=5',
                '-ms=on', '-mmt=on') }
    )
    # NOTE: zstd was probed and is NOT supported by 7-Zip 24.09's 7z
    # format (-m0=zstd fails with "The parameter is incorrect"), so the
    # benchmark compares LZMA2 levels only.
    $results = @()
    foreach ($c in $cands) {
        $arc = Join-Path $work ("{0}.{1}.7z" -f $outBase, $c.tag)
        Remove-Item $arc -Force -ErrorAction SilentlyContinue
        $zargs = $c.args
        Push-Location $baseDir
        & $sevenZip @zargs $arc "@$list" -bso0 -bsp0 | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "7z failed: $($c.tag)" }
        Pop-Location
        $results += [pscustomobject]@{
            tag = $c.tag; file = $arc
            size = (Get-Item $arc).Length
        }
    }
    return ($results | Sort-Object size | Select-Object -First 1)
}

function Publish-Package([string]$name, [string[]]$topDirs) {
    $uncomp = 0
    foreach ($d in $topDirs) { $uncomp += Get-TreeBytes (Join-Path $stage $d) }
    $best = Compress-Best $stage $topDirs $name
    $final = Join-Path $outDir ("$name.7z")
    Copy-Item -LiteralPath $best.file -Destination $final -Force
    $hash = (Get-FileHash -LiteralPath $final -Algorithm SHA256).Hash.ToLower()
    $files = foreach ($d in $topDirs) {
        $full = Join-Path $stage $d
        foreach ($f in (Get-RelFiles $full)) {
            $fi = Get-Item (Join-Path $full $f)
            [pscustomobject]@{
                path = (Join-Path $d $f) -replace '\\', '/'
                size = $fi.Length
                sha256 = (Get-FileHash -LiteralPath $fi.FullName `
                    -Algorithm SHA256).Hash.ToLower()
            }
        }
    }
    # Validate: extract to a clean dir, compare every byte.
    $vdir = Join-Path $work "verify-$name"
    Remove-Item -Recurse -Force $vdir -ErrorAction SilentlyContinue
    & $sevenZip x $final "-o$vdir" -bso0 -bsp0 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "extract failed: $name" }
    foreach ($f in $files) {
        $vf = Join-Path $vdir ($f.path -replace '/', '\')
        if (-not (Test-Path -LiteralPath $vf)) {
            throw "missing after extract: $($f.path)"
        }
        $vh = (Get-FileHash -LiteralPath $vf -Algorithm SHA256).Hash.ToLower()
        if ($vh -ne $f.sha256) { throw "hash mismatch: $($f.path)" }
    }
    $saved = $uncomp - (Get-Item $final).Length
    $pct = if ($uncomp -gt 0) {
        [math]::Round(100.0 * $saved / $uncomp, 1)
    } else { 0 }
    Write-Output ("PACKED {0}: {1:N0} -> {2:N0} bytes ({3}% saved, {4})" `
        -f $name, $uncomp, (Get-Item $final).Length, $pct, $best.tag)
    return [pscustomobject]@{
        filename = "$name.7z"
        version = $Version
        type = '7z'
        method = $best.tag
        uncompressedSize = $uncomp
        compressedSize = (Get-Item $final).Length
        savedPercent = $pct
        sha256 = $hash
        files = $files
    }
}

$core = Publish-Package 'LunarPlayer-Core' @('core', 'ffmpeg')
$ai = Publish-Package 'LunarPlayer-AI' @('ai')

$manifest = [pscustomobject]@{
    product = 'Lunar Player'
    version = $Version
    modelsIncluded = $false
    modelSource = 'official model source (download-on-demand, not hosted here)'
    packages = @($core, $ai)
}
$manifest | ConvertTo-Json -Depth 6 |
    Out-File -Encoding utf8 (Join-Path $outDir 'manifest.json')
Push-Location $outDir
Get-ChildItem (Join-Path $outDir '*.7z') -File | ForEach-Object {
    $h = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower()
    "$h  $($_.Name)"
} | Out-File -Encoding ascii (Join-Path $outDir 'SHA256SUMS.txt')
Pop-Location
Write-Output "EXTERNAL PACKAGES READY: $outDir"
Get-ChildItem $outDir -File | Select-Object Name, Length
