# ==============================================================================
#                   CHIMERA — START (no rebuild)
# ==============================================================================
$ErrorActionPreference = "Stop"

$repo = "C:\Chimera"
$exe  = "C:\Chimera\build\Release\Chimera.exe"

Set-Location $repo
git fetch origin | Out-Null
$localHead  = (git rev-parse HEAD).Trim()
$remoteHead = (git rev-parse origin/main).Trim()
if ($localHead -ne $remoteHead) {
    Write-Host "[ERROR] Local HEAD is behind origin/main." -ForegroundColor Red
    Write-Host "        Run REBUILD_AND_START.ps1 to pull/build exact latest commit." -ForegroundColor Yellow
    exit 1
}

if (-not (Test-Path $exe)) {
    Write-Host "[ERROR] Chimera.exe not found — run REBUILD_AND_START.ps1 first" -ForegroundColor Red
    exit 1
}
Write-Host "Starting Chimera at commit $localHead..." -ForegroundColor Cyan
cd C:\Chimera\build\Release
.\Chimera.exe chimera_config.ini
