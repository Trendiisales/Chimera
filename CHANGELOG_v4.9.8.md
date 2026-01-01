# Chimera v4.9.8 - FEE-AGNOSTIC ENTRY + GOVERNOR HEAT → SIZE SCALER

## Release Date: 2026-01-02

## Overview

v4.9.8 is the critical fee optimization release that fixes the **65-75% fee-only loss problem** and adds **institutional-grade governor heat → size scaling**. The core insight is that **gross edge exists** but **taker fills destroy it**, and **heat controls aggressiveness, not permissions**.

## The Problem (From CSV Analysis)

| Symbol | Trades | Issue | Root Cause |
|--------|--------|-------|------------|
| BTC | 1035 | 53.9% win rate, +0.0063 avg | 65-75% losses are fee-only |
| ETH | 699 | 7.1% win rate, -0.0009 avg | Taker dominates, gross edge exists |
| SOL | 1 | Dead | Over-restricted filters |

**Key Insight**: Trades have positive gross PnL but negative net PnL due to taker fees.

## The Fix

### 1. Fee-Agnostic Entry (THE CRITICAL CHANGE)

**OLD (Wrong)**:
```cpp
// Entry checked net edge (after fees)
if (edge_bps > cost_bps + margin) enter();
// This teaches: "If fees exist, don't trade"
```

**NEW (Correct)**:
```cpp
// Entry checks GROSS edge only (direction + microstructure)
if (gross_edge_bps > entry_params_.min_gross_edge_bps) enter();
// Exit checks NET positive (must clear fees)
if (net_pnl_bps > 0) confirmWinner();
```

### 2. Symbol-Specific Routing Modes

| Symbol | Mode | Rationale |
|--------|------|-----------|
| BTC | `MAKER_ONLY` | Speed edge destroyed by 8 bps round-trip taker fees |
| ETH | `HYBRID` | Maker-first, taker allowed if edge > 1.6 bps |
| SOL | `HYBRID` | Maker-first, taker allowed if edge > 2.8 bps |

### 3. Governor Heat → Size Scaler (NEW!)

**Core Principle**: Heat controls aggressiveness, not permissions.
- Heat ↑ → size ↓
- Heat ↓ → size ↑
- Trades may still occur when hot, but small and survivable

**Size Scaling Function** (piecewise for stable regimes):
```cpp
double governorHeatToSizeMultiplier(double heat) {
    if (heat <= 0.3) return 1.00;  // full size
    if (heat <= 0.6) return 0.75;  // mild caution
    if (heat <= 0.8) return 0.50;  // defensive
    return 0.25;                    // survival mode
}
```

**Symbol-Specific Size Caps**:
| Symbol | Max Size | Min Size |
|--------|----------|----------|
| BTC    | 1.0x     | 0.25x    |
| ETH    | 1.0x     | 0.30x    |
| SOL    | 0.8x     | 0.35x    |

**Heat-Aware Kill Interaction**:
```cpp
// Kill switches decide IF (zero-tolerance)
if (profile_killed || global_kill) final_size = 0.0;
// Governor heat decides HOW MUCH (graduated control)
```

### 4. Governor State Persistence (NEW!)

State now **survives restarts**:
- Saved to `state/governor_BTCUSDT.txt` etc.
- Loaded on startup
- Saved every 30 seconds

**Why this matters**:
- Before: Restart → reset → aggressive re-entry into bad regime → same losses
- Now: Restart → restore → appropriate caution → gradual recovery

### 5. CryptoSafetyGovernor (Auto-Relax/Auto-Tighten)

Institutional-grade self-regulation that automatically adjusts entry selectivity:

**Governor Priority (evaluated in order)**:
1. **Hard Bounds** - Absolute limits that can never be crossed
2. **Fee Dominance Governor** - Kills silent bleed (fee_dominance > 85%)
3. **Negative Drift Governor** - Anti-chop (net_expectancy < -0.05 bps)
4. **Over-Relax Governor** - Prevents filter collapse (max 3 relax steps)
5. **Auto-Relax/Auto-Tighten** - Normal operation
6. **Recovery Governor** - Controlled re-entry after clamps

**Hard Bounds Per Symbol**:
```
BTCUSDT:
  confidence: [0.55, 0.72]
  expectancy_bps: [0.20, 0.45]
  maker_timeout_ms: [180, 420]
  
ETHUSDT:
  confidence: [0.58, 0.72]
  expectancy_bps: [0.25, 0.55]
  maker_timeout_ms: [140, 300]
  
SOLUSDT:
  confidence: [0.56, 0.75]
  expectancy_bps: [0.35, 0.80]
  maker_timeout_ms: [80, 200]
```

### 6. New Safety Guards

**FeeLossGuard**:
- Tracks trades where gross > 0 but net < 0 (fee-only losses)
- If 3+ fee-only losses in 10 trades → reduce size, force maker

**TakerContaminationGuard**:
- Per-symbol taker thresholds:
  - BTC: >5% taker → force maker
  - ETH: >18% taker → force maker
  - SOL: >25% taker → force maker

**DynamicMakerTimeout**:
```
timeout = base + (queue_pos * 18) - (vol * 22) + (fill_deficit * 40)

BTC: base=220, min=160, max=420, target_fill=70%
ETH: base=160, min=120, max=300, target_fill=55%
SOL: base=100, min=80, max=200, target_fill=40%
```

### 7. Dashboard Telemetry

New Governor Heat panel with per-symbol display:
- Heat value (0.0 - 1.0)
- Heat bar visualization (green/yellow/orange/red)
- Size multiplier
- Governor state

**Heat Interpretation**:
| Heat | Color | Meaning |
|------|-------|---------|
| 0.0-0.3 | Green | Relaxed / Aggressive |
| 0.3-0.6 | Yellow | Normal |
| 0.6-0.8 | Orange | Cautious |
| 0.8-1.0 | Red | Survival / Clamped |

## Files Changed

### New Files
- `crypto_engine/include/safety/CryptoSafetyGovernor.hpp`
  - Governor heat calculation
  - Size scaling functions
  - DecisionTrace struct
- `crypto_engine/include/safety/FeeLossGuard.hpp`
- `crypto_engine/include/safety/GovernorPersistence.hpp`
  - State save/load functions

### Modified Files
- `crypto_engine/include/microscalp/CryptoMicroScalp.hpp`
  - `calcEdgeBps()` → `calcGrossEdgeBps()` (NO FEES in entry gate)
  - Added `calcNetPnlBps()` for exit validation
  - Added `governorHeat()` and `sizeMultiplierFromHeat()` accessors
  - Added `saveState()` / `loadState()` methods
  - Updated `sizeMultiplier()` to use governor heat scaling
  
- `src/main_triple.cpp`
  - BTC = MAKER_ONLY, ETH/SOL = HYBRID
  - Fill callback routes to correct engine
  - Governor state load on startup
  - Periodic state save (every 30s)
  - Governor heat telemetry updates
  
- `include/gui/GUIBroadcaster.hpp`
  - GovernorHeatData struct
  - updateGovernorHeat() method
  - JSON output for dashboard
  
- `chimera_dashboard.html`
  - Governor Heat panel with bars, colors, state display

## Why This Solves the Last Failure Mode

Previous pattern:
1. System detects edge ✔
2. Execution degrades ❌
3. You tighten filters ✔
4. Nothing trades ❌
5. Restart resets everything ❌
6. Full size re-enters bad regime ❌

Now:
1. Bad regime → heat ↑
2. Size ↓ automatically
3. Learning continues (small trades)
4. Recovery happens gradually
5. Size re-expands only when heat falls
6. State persists across restarts

**This is how real desks survive regime shifts.**

## Deployment

```bash
cd ~
mv Chimera Chimera_archive_$(date +%Y%m%d_%H%M%S)
cp /mnt/c/Chimera/chimera_v4_9_8.zip ~/
unzip chimera_v4_9_8.zip
mv chimera_src Chimera
cd ~/Chimera && mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release && make -j4
cd .. && ./build/chimera
```

## Validation

Monitor these metrics after deployment:
1. **Governor heat** - Should reflect regime quality
2. **Size multiplier** - Should scale with heat
3. **Fee-only loss ratio** - Should decrease significantly
4. **Maker fill rate** - BTC should be ~100% maker
5. **Governor state** - Should stay in NORMAL unless conditions degrade
6. **State files** - Should appear in `state/` directory

## Previous Version

v4.9.7 - Fixed fill callback deadlock, used TAKER_ONLY as safety fallback
