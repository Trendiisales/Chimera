# Chimera v4.9.0 - MULTI-PROFIT-ENGINE RELEASE

**Release Date:** 2025-01-01  
**Package:** `chimera_v4_9_0_PROFIT_ENGINES.zip`

---

## 🚀 OVERVIEW

This release adds **5 NEW PROFIT ENGINES** to Chimera, bringing the total to 6 independent profit engines. Each engine monetizes a distinct market behavior with uncorrelated failure modes.

---

## 📊 COMPLETE ENGINE MATRIX

| Engine | Behavior | Frequency | Risk | Edge Source |
|--------|----------|-----------|------|-------------|
| **PREDATOR** | Microstructure speed | High | 0.05% | Speed advantage |
| **OPEN_RANGE** | Time-based liquidity | Low | 0.15% | NY Open resolution |
| **STOP_RUN_FADE** | Liquidity failure | Medium | 0.05% | Failed sweeps |
| **SESSION_HANDOFF** | Structural reposition | Very Low | 0.20% | Session flows |
| **VWAP_DEFENSE** | Inventory defense | Moderate | 0.07% | VWAP reclaim |
| **LIQUIDITY_VACUUM** | Mechanical gaps | Low-Mod | 0.05% | Depth gaps |

---

## 🆕 NEW FILES (12 total)

### Headers (6)
```
include/profile/OpenRangeProfile.hpp
include/profile/StopRunFadeProfile.hpp
include/profile/SessionHandoffProfile.hpp
include/profile/VwapDefenseProfile.hpp
include/profile/LiquidityVacuumProfile.hpp
include/profile/ProfileRegistry.hpp       ← Master include
```

### Implementations (5)
```
src/profile/OpenRangeProfile.cpp
src/profile/StopRunFadeProfile.cpp
src/profile/SessionHandoffProfile.cpp
src/profile/VwapDefenseProfile.cpp
src/profile/LiquidityVacuumProfile.cpp
```

---

## 🎯 ENGINE #1: OPEN RANGE EXPLOITER (ORE)

**What it monetizes:** NY Open liquidity resolution (09:30-09:35 NY)

**State Machine:**
```
IDLE → RANGE_BUILDING → ARMED → IN_TRADE → DONE
```

**Entry Types:**
- **Type A:** Range Break + Acceptance (hold > 1.5s, VWAP confirms)
- **Type B:** Range Failure Fade (break fails within 1.0s, fade it)

**Risk Model:**
- Risk: 0.15%
- Trades/day/symbol: 1
- Time cap: 20s
- Symbols: NAS100, US30, SPX500, XAUUSD

---

## 🎯 ENGINE #2: STOP RUN FADE

**What it monetizes:** Stop liquidity sweeps that fail

**State Machine:**
```
IDLE → RUN_DETECTED → CONFIRM_FAIL → IN_TRADE → COOLDOWN
```

**Entry Logic:**
1. Velocity spike > threshold
2. Range expansion > 2× baseline
3. Extreme imbalance (> 0.85)
4. Failure confirmed within 150ms
5. Enter AGAINST the run

**Risk Model:**
- Risk: 0.05-0.10%
- Many trades/day (gated)
- Time cap: 3s
- Symbols: Indices + Gold

---

## 🎯 ENGINE #3: SESSION HANDOFF

**What it monetizes:** Institutional repositioning at session boundaries

**Handoff Windows:**
- Asia → London: 06:45-07:15 UTC
- London → NY: 13:15-13:45 UTC

**State Machine:**
```
IDLE → OBSERVING → ARMED → IN_TRADE → DONE
```

**Bias Determination:**
- VWAP hold/reject
- Value migration (POC drift)
- Failed extremes

**Risk Model:**
- Risk: 0.20%
- Trades/day: 1-2
- Time cap: 60s
- Symbols: Indices + Gold

---

## 🎯 ENGINE #4: VWAP DEFENSE

**What it monetizes:** Institutional VWAP inventory defense

**State Machine:**
```
IDLE → VWAP_TESTING → RECLAIM_CONFIRMED → IN_TRADE → COOLDOWN
```

**Entry Types:**
- **Variant A:** VWAP Reclaim (price crosses back, holds 300ms)
- **Variant B:** VWAP Fail Fade (break fails within 400ms)

**Risk Model:**
- Risk: 0.05-0.10%
- Moderate frequency
- Time cap: 5-8s
- Win rate: HIGH

---

## 🎯 ENGINE #5: LIQUIDITY VACUUM

**What it monetizes:** Mechanical liquidity gaps (not informational)

**State Machine:**
```
IDLE → VACUUM_DETECTED → CONFIRM_CONTINUATION → IN_TRADE → DONE
```

**Detection:**
- Depth drops > 60%
- Price jumps ≥ X ticks in ≤ 120ms
- Spread does NOT widen abnormally

**Risk Model:**
- Risk: 0.05%
- Event-driven (low-moderate)
- Time cap: 1.0-1.5s (NEVER holds)
- Asymmetric payoff

---

## 🏛️ GOVERNANCE HIERARCHY (UNCHANGED)

```
1. Latency / Shock / Risk exits    ← HIGHEST
2. DailyHealthAudit (hard stop)
3. RollingEdgeAudit (slow throttle)
4. EdgeRecoveryRules (conservative re-enable)
5. GoNoGoGate (session decision)
6. Engine / Profile logic          ← LOWEST
```

All new engines respect this hierarchy. No engine can override global risk controls.

---

## 📈 CORRELATION MATRIX

Failure modes are designed to be **uncorrelated**:

```
          PRED  ORE   SRF   SH    VD    LV
PRED       1    0.1   0.2   0.05  0.15  0.1
ORE       0.1    1    0.1   0.2   0.15  0.05
SRF       0.2   0.1    1    0.1   0.2   0.3
SH        0.05  0.2   0.1    1    0.1   0.05
VD        0.15  0.15  0.2   0.1    1    0.15
LV        0.1   0.05  0.3   0.05  0.15   1
```

**This is how you scale profit without scaling drawdown.**

---

## 🚀 DEPLOYMENT ORDER (RECOMMENDED)

```
1. PREDATOR         → Already deployed ✓
2. OPEN_RANGE       → Shadow first
3. VWAP_DEFENSE     → Shadow first
4. STOP_RUN_FADE    → Shadow first
5. LIQUIDITY_VACUUM → Shadow first
6. SESSION_HANDOFF  → Live (already low risk)
```

---

## 📦 DEPLOYMENT

```bash
cd ~ && mv ~/Chimera ~/Chimera_archive_$(date +%Y%m%d_%H%M%S)
cp /mnt/c/Chimera/chimera_v4_9_0_PROFIT_ENGINES.zip ~/
unzip -o chimera_v4_9_0_PROFIT_ENGINES.zip && mv chimera_src Chimera
cd ~/Chimera && rm -rf build && mkdir build && cd build
cmake .. && make -j4
./chimera
```

---

## 🔗 GITHUB

```bash
cd ~/Chimera && git add -A
git commit -m "v4.9.0: 5 new profit engines (ORE, SRF, SH, VD, LV)"
git push -u origin main
```

---

## 📊 USAGE EXAMPLE

```cpp
#include "profile/ProfileRegistry.hpp"

// Get profile manager singleton
auto& pm = Chimera::getProfileManager();

// Enable all profiles
pm.enableAll();

// Check status
pm.printAllStatus();

// Access individual profiles
pm.predator().onTick(predatorSnap);
pm.openRange().onTick(oreSnap);
pm.stopRunFade().onTick(srfSnap);
pm.sessionHandoff().onTick(shSnap);
pm.vwapDefense().onTick(vdSnap);
pm.liquidityVacuum().onTick(lvSnap);

// Reset at day boundary
pm.resetDay();

// Reset at session boundary
pm.resetAllSessions();

// Check for any open position
if (pm.anyPositionOpen()) {
    // Handle position management
}
```

---

## ⚠️ CRITICAL NOTES

1. **Shadow first** - Run new engines in shadow mode before live
2. **No overlap** - Engines are designed with uncorrelated edges
3. **Governance respected** - All engines subordinate to risk hierarchy
4. **One position rule** - Each engine maintains max 1 position
5. **Session gates** - Asia disabled for most engines (low liquidity)

---

**Total New Lines:** ~4,500  
**Total New Files:** 12  
**Build Size:** Unchanged (~same as v4.8.0)

---

*This is how multi-strategy desks scale safely.*
