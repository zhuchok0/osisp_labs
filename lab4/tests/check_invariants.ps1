$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$exe = "build\main.exe"
if (-not (Test-Path $exe)) {
    Write-Host "ERROR: $exe not found. Build first." -ForegroundColor Red
    exit 1
}

$output = & $exe --readers 3 --writers 5 --ops 20 --quiet
$output | Out-Host

$viol = ($output | Select-String "Invariant violations:\s+(\d+)").Matches[0].Groups[1].Value
$tmo  = ($output | Select-String "Timeouts total:\s+(\d+)").Matches[0].Groups[1].Value

if ($viol -ne "0") {
    Write-Host "FAIL: invariant violations = $viol" -ForegroundColor Red
    exit 1
}

Write-Host "OK: invariants hold (violations=0)" -ForegroundColor Green
Write-Host "INFO: timeouts total = $tmo (metric, not a failure for writers-preference)" -ForegroundColor Yellow
exit 0