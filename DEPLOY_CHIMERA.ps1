# ==============================================================================
#                        CHIMERA - DEPLOY AND START
#   Run from anywhere on the VPS - handles everything.
# ==============================================================================

Write-Host "=======================================================" -ForegroundColor Cyan
Write-Host "   CHIMERA  |  Commodities and Indices  |  Breakout System" -ForegroundColor Cyan
Write-Host "=======================================================" -ForegroundColor Cyan
Write-Host ""

# [1/4] Stop any running Chimera process
Write-Host "[1/4] Stopping existing Chimera process..." -ForegroundColor Yellow
Stop-Process -Name "Chimera" -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Write-Host "      [OK] Stopped (or was not running)" -ForegroundColor Green
Write-Host ""

# [2/4] Pull latest from GitHub
Write-Host "[2/4] Pulling latest from GitHub..." -ForegroundColor Yellow
Set-Location C:\Chimera
git fetch origin
git reset --hard origin/main
Write-Host "      [OK] Up to date: $(git log --oneline -1)" -ForegroundColor Green
Write-Host ""

# [3/4] Build
Write-Host "[3/4] Building..." -ForegroundColor Yellow
if (Test-Path "C:\Chimera\build") {
    Remove-Item -Path "C:\Chimera\build" -Recurse -Force
}
New-Item -ItemType Directory -Path "C:\Chimera\build" -Force | Out-Null
Set-Location C:\Chimera\build
cmake .. -DCMAKE_BUILD_TYPE=Release 2>&1 | Out-Null
cmake --build . --config Release
if (-not (Test-Path "C:\Chimera\build\Release\Chimera.exe")) {
    Write-Host "      [ERROR] Build failed - Chimera.exe not found!" -ForegroundColor Red
    Read-Host "Press Enter to exit"
    return
}
Write-Host "      [OK] Chimera.exe built" -ForegroundColor Green
Write-Host ""

# [4/4] Copy assets and run
Write-Host "[4/4] Copying assets and starting..." -ForegroundColor Yellow
$rel = "C:\Chimera\build\Release"
$configSource = "C:\Chimera\config\chimera_config.ini"
if (-not (Test-Path $configSource)) { $configSource = "C:\Chimera\chimera_config.ini" }
if (-not (Test-Path $configSource)) {
    Write-Host "      [ERROR] chimera_config.ini not found in repo" -ForegroundColor Red
    Read-Host "Press Enter to exit"
    return
}
Copy-Item $configSource                          "$rel\chimera_config.ini"  -Force
Copy-Item "C:\Chimera\src\gui\www\chimera_index.html" "$rel\chimera_index.html" -Force -ErrorAction SilentlyContinue
Copy-Item "C:\Chimera\src\gui\www\chimera_logo.png" "$rel\chimera_logo.png" -Force -ErrorAction SilentlyContinue
Write-Host "      [OK] Assets copied" -ForegroundColor Green
Write-Host ""

Write-Host "=======================================================" -ForegroundColor Cyan
Write-Host "  Starting Chimera.exe..." -ForegroundColor Cyan
Write-Host "  GUI -> http://185.167.119.59:7779" -ForegroundColor Green
Write-Host "=======================================================" -ForegroundColor Cyan
Write-Host ""

Set-Location $rel
.\Chimera.exe chimera_config.ini
