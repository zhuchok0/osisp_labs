$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$exe = "build\main.exe"
if (-not (Test-Path $exe)) {
    Write-Host "ERROR: $exe not found. Build first." -ForegroundColor Red
    exit 1
}

$configs = @(
    @{ R = 3; W = 2; Name = "3R/2W" },
    @{ R = 3; W = 5; Name = "3R/5W" }
)

foreach ($c in $configs) {
    Write-Host "=== Test $($c.Name) ===" -ForegroundColor Cyan
    & $exe --readers $c.R --writers $c.W --ops 10 --quiet
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAIL: exit code $LASTEXITCODE" -ForegroundColor Red
        exit 1
    }
}

Write-Host "All tests passed" -ForegroundColor Green