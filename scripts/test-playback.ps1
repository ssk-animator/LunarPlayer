param(
    [string]$VideoPath = "C:\Users\ssk90\Downloads\ame4sub_[www.AnimeKhor.org].mp4",
    [string]$VideoLabel = "8bit-mp4",
    [int]$Duration = 30,
    [string]$BuildConfig = "Release"
)

$ProjectDir = "C:\Users\ssk90\Downloads\Lunar Player Project by SSK\Lunar Player"
$BuildDir = "$ProjectDir\build"
$ExePath = "$BuildDir\$BuildConfig\LunarPlayer.exe"
$LogDir = "$ProjectDir\test-results"
$Timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$TestName = "${VideoLabel}_${Timestamp}"
$StderrLog = "$LogDir\${TestName}_stderr.log"
$ReportFile = "$LogDir\${TestName}_report.txt"

if (!(Test-Path $LogDir)) { New-Item -ItemType Directory -Path $LogDir -Force | Out-Null }

Write-Host "=== PHASE 3.5 TEST HARNESS ===" -ForegroundColor Cyan
Write-Host "Video: $VideoLabel ($VideoPath)"
Write-Host "Duration: ${Duration}s"
Write-Host "Test: $TestName"

# Step 1: Build
Write-Host "`n[1/6] Building..." -ForegroundColor Yellow
Set-Location $BuildDir
$buildOutput = cmake --build . --config $BuildConfig --target LunarPlayer -j 8 2>&1
$buildOk = ($LASTEXITCODE -eq 0)
if (!$buildOk) {
    Write-Host "BUILD FAILED" -ForegroundColor Red
    $buildOutput | Select-Object -Last 10
    "BUILD FAILED`n$($buildOutput | Select-Object -Last 20)" | Out-File $ReportFile
    exit 1
}
Write-Host "BUILD OK" -ForegroundColor Green

# Step 2: Launch
Write-Host "`n[2/6] Launching LunarPlayer..." -ForegroundColor Yellow
Remove-Item $StderrLog -ErrorAction SilentlyContinue
$proc = Start-Process -FilePath $ExePath -ArgumentList "`"$VideoPath`"" `
    -RedirectStandardError $StderrLog -PassThru -NoNewWindow
Write-Host "PID: $($proc.Id)"

# Step 3: Wait for init + playback
Write-Host "`n[3/6] Monitoring playback for ${Duration}s..." -ForegroundColor Yellow
$startTime = Get-Date
$peakMemory = 0
$stallDetected = $false
$crashDetected = $false
$notResponding = $false

Start-Sleep -Seconds 3  # Let app initialize

for ($i = 0; $i -lt $Duration; $i += 2) {
    Start-Sleep -Seconds 2
    try {
        $p = Get-Process -Id $proc.Id -ErrorAction Stop
        $memMB = [math]::Round($p.WorkingSet64 / 1MB, 1)
        if ($memMB -gt $peakMemory) { $peakMemory = $memMB }

        # Check responsiveness
        if ($p.Responding -eq $false) {
            $notResponding = $true
            Write-Host "  [${i}s] NOT RESPONDING (mem=${memMB}MB)" -ForegroundColor Red
        } else {
            Write-Host "  [${i}s] OK (mem=${memMB}MB)" -ForegroundColor DarkGray
        }
    } catch {
        $crashDetected = $true
        Write-Host "  [${i}s] PROCESS CRASHED" -ForegroundColor Red
        break
    }
}

# Step 4: Capture final state
Write-Host "`n[4/6] Capturing results..." -ForegroundColor Yellow
$exitCode = -1
$playbackDuration = 0
try {
    if (!$proc.HasExited) {
        $proc.Kill()
        $proc.WaitForExit(5000)
    }
    $exitCode = $proc.ExitCode
    $playbackDuration = ((Get-Date) - $startTime).TotalSeconds
} catch {
    $exitCode = -999
}

# Step 5: Analyze stderr
Write-Host "`n[5/6] Analyzing logs..." -ForegroundColor Yellow
$stderrContent = ""
if (Test-Path $StderrLog) {
    $stderrContent = Get-Content $StderrLog -Raw
}

$lines = $stderrContent -split "`n"
$tickLines = $lines | Where-Object { $_ -match '\[TICK' }
$profileLines = $lines | Where-Object { $_ -match '\[PROFILE\]' }
$pipeLines = $lines | Where-Object { $_ -match '\[PIPE\]' }
$queueLines = $lines | Where-Object { $_ -match '\[QUEUE\]' }

# Extract frame numbers and timings
$frameNums = @()
$tickTimes = @()
$decodeTimes = @()
$uploadTimes = @()
$queueSizes = @()

foreach ($line in $tickLines) {
    if ($line -match 'F(\d+)') { $frameNums += [int]$Matches[1] }
    if ($line -match 'tick=([\d.]+)') { $tickTimes += [double]$Matches[1] }
    if ($line -match 'decode=([\d.]+)') { $decodeTimes += [double]$Matches[1] }
    if ($line -match 'upload=([\d.]+)') { $uploadTimes += [double]$Matches[1] }
    if ($line -match 'queue=(\d+)') { $queueSizes += [int]$Matches[1] }
}

$maxFrame = if ($frameNums.Count -gt 0) { ($frameNums | Measure-Object -Maximum).Maximum } else { 0 }
$avgTick = if ($tickTimes.Count -gt 0) { [math]::Round(($tickTimes | Measure-Object -Average).Average, 1) } else { 0 }
$peakTick = if ($tickTimes.Count -gt 0) { [math]::Round(($tickTimes | Measure-Object -Maximum).Maximum, 1) } else { 0 }
$avgDecode = if ($decodeTimes.Count -gt 0) { [math]::Round(($decodeTimes | Measure-Object -Average).Average, 1) } else { 0 }
$peakDecode = if ($decodeTimes.Count -gt 0) { [math]::Round(($decodeTimes | Measure-Object -Maximum).Maximum, 1) } else { 0 }
$avgUpload = if ($uploadTimes.Count -gt 0) { [math]::Round(($uploadTimes | Measure-Object -Average).Average, 1) } else { 0 }
$peakUpload = if ($uploadTimes.Count -gt 0) { [math]::Round(($uploadTimes | Measure-Object -Maximum).Maximum, 1) } else { 0 }
$avgQueue = if ($queueSizes.Count -gt 0) { [math]::Round(($queueSizes | Measure-Object -Average).Average, 1) } else { 0 }

$egagainCount = ($pipeLines | Where-Object { $_ -match 'EAGAIN' }).Count
$lateDrops = ($queueLines | Where-Object { $_ -match 'dropping late' }).Count
$tickCount = $tickLines.Count
$avgFps = if ($playbackDuration -gt 0 -and $maxFrame -gt 0) { [math]::Round($maxFrame / $playbackDuration, 1) } else { 0 }

# Find FPS dips (ticks with >80ms)
$slowTicks = ($tickTimes | Where-Object { $_ -gt 80 }).Count

# Step 6: Report
Write-Host "`n[6/6] Generating report..." -ForegroundColor Yellow

$report = @"
=== LUNAR PLAYER TEST REPORT ===
Test: $TestName
Video: $VideoLabel
File: $VideoPath
Date: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
Duration: ${Duration}s (actual: $([math]::Round($playbackDuration, 1))s)

=== BUILD ===
Status: OK

=== PLAYBACK ===
Process Exit Code: $exitCode
Peak Memory: ${peakMemory}MB
Crash Detected: $crashDetected
Not Responding: $notResponding

=== PERFORMANCE ===
Total Frames: $maxFrame
Avg FPS: $avgFps
Avg Tick: ${avgTick}ms
Peak Tick: ${peakTick}ms
Avg Decode: ${avgDecode}ms
Peak Decode: ${peakDecode}ms
Avg Upload: ${avgUpload}ms
Peak Upload: ${peakUpload}ms
Avg Queue Depth: $avgQueue
Slow Ticks (>80ms): $slowTicks / $tickCount
EAGAIN Events: $egagainCount
Late Frame Drops: $lateDrops
Total Ticks: $tickCount

=== PROFILER ===
$($profileLines -join "`n")

=== DIAGNOSTIC LINES (first 20) ===
$($tickLines | Select-Object -First 20 | ForEach-Object { $_ })

=== DIAGNOSTIC LINES (last 20) ===
$($tickLines | Select-Object -Last 20 | ForEach-Object { $_ })
"@

$report | Out-File $ReportFile -Encoding UTF8
Write-Host $report -ForegroundColor Cyan

Write-Host "`n=== TEST COMPLETE ===" -ForegroundColor Green
Write-Host "Report: $ReportFile"
Write-Host "Full log: $StderrLog"
