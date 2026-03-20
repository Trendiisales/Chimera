# Chimera — Commodities & Indices Trading System

**Strategy:** Compression Breakout (CRTP engine, zero virtual dispatch)  
**Broker:** BlackBull Markets — same FIX stack as ChimeraMetals  
**Primary symbols:** US500.F · USTEC.F · USOIL.F  
**Extended symbols:** DJ30.F · GER30 · UK100 · ESTX50 · XAGUSD · EURUSD · UKBRENT  
**GUI:** HTTP :7779 / WebSocket :7780  
**Mode:** Shadow (default)

## Build (Windows + MSVC)
```
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

## Run
```
build\Release\Chimera.exe chimera_config.ini
```

## GUI
Open `http://localhost:7779` in browser.

## Baseline Report (PowerShell on VPS)
Run expectancy/profit-factor summary from the full trade CSV:

```powershell
Set-Location C:\Chimera
powershell -ExecutionPolicy Bypass -File .\BASELINE_REPORT.ps1 -CsvPath "C:\Chimera\build\Release\logs\trades\chimera_trade_closes.csv" -MinTrades 30
```
