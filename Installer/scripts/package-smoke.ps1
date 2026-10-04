# Lunar Player packaging mechanics proof (Phase: packaging pipeline).
# Proves heat.exe -> candle.exe -> light.exe produce a valid MSI from a
# scratch payload WITHOUT touching the real 16 GB tree. The production
# package-release script reuses these exact mechanics against the staged
# payload (exclusion manifest + heat harvest + version binding).
# Usage: package-smoke.ps1  (exit 0 = mechanics proven)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$wix = Join-Path $root 'thirdparty\wix314'
$work = Join-Path ([System.IO.Path]::GetTempPath()) 'LunarPkgSmoke'
Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
$stage = Join-Path $work 'stage\App'
New-Item -ItemType Directory -Path $stage -Force | Out-Null
'payload' | Out-File -Encoding ascii (Join-Path $stage 'app.txt')
New-Item -ItemType Directory -Path (Join-Path $stage 'data') -Force | Out-Null
'data' | Out-File -Encoding ascii (Join-Path $stage 'data\blob.bin')
$obj = Join-Path $work 'obj'
New-Item -ItemType Directory -Path $obj -Force | Out-Null

& "$wix\heat.exe" dir "$stage" -cg SmokePayload -gg -scom -sreg -sfrag `
    -dr INSTALLFOLDER -var var.StageDir `
    -out (Join-Path $obj 'payload.wxs')
if ($LASTEXITCODE -ne 0) { throw 'heat failed' }

$product = @'
<?xml version="1.0" encoding="UTF-8"?>
<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi">
  <Product Id="*" Name="LunarPkgSmoke" Language="1033" Version="1.0.0.0"
           Manufacturer="SSK" UpgradeCode="11111111-2222-3333-4444-555555555555">
    <Package InstallerVersion="500" Compressed="yes" InstallScope="perMachine" />
    <MajorUpgrade DowngradeErrorMessage="Newer installed." />
    <MediaTemplate EmbedCab="yes" />
    <Directory Id="TARGETDIR" Name="SourceDir">
      <Directory Id="ProgramFiles64Folder">
        <Directory Id="INSTALLFOLDER" Name="LunarPkgSmoke" />
      </Directory>
    </Directory>
    <Feature Id="Main" Title="Main" Level="1">
      <ComponentGroupRef Id="SmokePayload" />
    </Feature>
  </Product>
</Wix>
'@
$product | Out-File -Encoding utf8 (Join-Path $obj 'product.wxs')

& "$wix\candle.exe" -arch x64 "-dStageDir=$stage" `
    -out "$obj\\" (Join-Path $obj 'payload.wxs') (Join-Path $obj 'product.wxs')
if ($LASTEXITCODE -ne 0) { throw 'candle failed' }

$msi = Join-Path $work 'smoke.msi'
& "$wix\light.exe" -out $msi (Join-Path $obj 'payload.wixobj') `
    (Join-Path $obj 'product.wixobj') -ext "$wix\WixUIExtension.dll"
if ($LASTEXITCODE -ne 0) { throw 'light failed' }
$size = (Get-Item $msi).Length
if ($size -lt 1024) { throw 'MSI implausibly small' }
Write-Output "PACKAGE MECHANICS PROVEN: $msi ($size bytes)"
