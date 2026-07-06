param(
    [string]$ExePath = "C:\Users\ssk90\Downloads\Lunar Player Project by SSK\Lunar Player\build_audio\Release\LunarPlayer.exe",
    [string]$LogFile = "C:\Users\ssk90\Downloads\test_suite_results.log"
)

$testFiles = @(
    @{ Name = "AAC Stereo (8-bit)"; Path = "C:\Users\ssk90\Downloads\ame4sub_[www.AnimeKhor.org].mp4"; ExpectedCodec = "aac"; MaxUnderruns = 20 },
    @{ Name = "EAC3 5.1 (10-bit HDR)"; Path = "C:\Users\ssk90\Downloads\Mortal Kombat II (2026) 2160p MA WEB-DL DV HDR10 HEVC [Hindi DDP 5.1 + English DDP Atmos 5.1] (HONE-UNITYhub).mkv"; ExpectedCodec = "eac3"; MaxUnderruns = 50 }
)

$results = @()
Get-Process -Name LunarPlayer -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

foreach ($test in $testFiles) {
    $testLog = "C:\Users\ssk90\Downloads\test_$($test.Name -replace '[^a-zA-Z0-9]', '_').log"
    
    Write-Host "=== Testing: $($test.Name) ==="
    
    if (-not (Test-Path $test.Path)) {
        Write-Host "  SKIP: File not found"
        $results += [PSCustomObject]@{ Test = $test.Name; Status = "SKIP"; Reason = "File not found" }
        continue
    }
    
    $proc = Start-Process -FilePath $ExePath -ArgumentList "`"$($test.Path)`"" -RedirectStandardError $testLog -PassThru
    Start-Sleep -Seconds 12
    
    $running = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
    $crashed = ($null -eq $running)
    
    $logContent = Get-Content $testLog -Raw
    $hasCrash = $logContent -match "CRASH|SEGFAULT|ACCESS_VIOLATION|unhandled exception"
    $hasDecoder = $logContent -match "\[AUDIO-ENGINE\] Decoder:"
    $hasPipeline = $logContent -match "\[AUDIO-ENGINE\] Pipeline:"
    $hasPassthrough = $logContent -match "No passthrough for"
    
    $underrunMatch = [regex]::Match($logContent, "underruns=(\d+)")
    $underruns = if ($underrunMatch.Success) { [int]$underrunMatch.Groups[1].Value } else { 0 }
    
    $overrunMatch = [regex]::Match($logContent, "overruns=(\d+)")
    $overruns = if ($overrunMatch.Success) { [int]$overrunMatch.Groups[1].Value } else { 0 }
    
    if ($crashed) {
        Write-Host "  FAIL: Process exited during playback (crash)"
        $results += [PSCustomObject]@{ Test = $test.Name; Status = "CRASH"; Reason = "Process exited" }
    } elseif ($hasCrash) {
        Write-Host "  FAIL: Crash detected in log"
        $results += [PSCustomObject]@{ Test = $test.Name; Status = "CRASH"; Reason = "Crash in log" }
    } elseif (-not $hasDecoder) {
        Write-Host "  FAIL: No audio decoder found"
        $results += [PSCustomObject]@{ Test = $test.Name; Status = "FAIL"; Reason = "No audio decoder" }
    } elseif (-not $hasPipeline) {
        Write-Host "  FAIL: No audio pipeline started"
        $results += [PSCustomObject]@{ Test = $test.Name; Status = "FAIL"; Reason = "No pipeline" }
    } else {
        $status = "PASS"
        $reason = "underruns=$underruns overruns=$overruns"
        if ($underruns -gt $test.MaxUnderruns) {
            $status = "WARN"
            $reason += " (high underruns)"
        }
        Write-Host "  $status : $reason"
        $results += [PSCustomObject]@{ Test = $test.Name; Status = $status; Reason = $reason }
    }
    
    Get-Process -Name LunarPlayer -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Seconds 1
}

Write-Host "`n=== SUMMARY ==="
$results | Format-Table -AutoSize

$results | Out-File $LogFile -Encoding UTF8
Write-Host "Results saved to $LogFile"
