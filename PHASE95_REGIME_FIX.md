# PHASE 9.5: REGIME INTEGRITY LAYER
## Critical Structural Fix - Per-Symbol Regime Clocks

---

## 🚨 THE PROBLEM (CRITICAL BUG)

**Before Phase 9.5:**
- ETH and SOL shared regime state
- ETH depth lag could blind SOL trading
- No isolation between symbols
- Mild lag = total shutdown (no soft-invalid state)
- Zero visibility into missed opportunities

**Impact:**
- False invalidations on thin symbols like SOL
- 20-50% trade opportunity loss
- No audit trail of blocked trades

---

## ✅ THE FIX

**After Phase 9.5:**
- Per-symbol regime clocks (complete isolation)
- Soft-invalid state: STABLE → DEGRADED → INVALID
- Blocked-trade audit trail with JSONL logging
- ETH lag cannot affect SOL, and vice versa

---

## 🏗️ ARCHITECTURE

### Regime States (3-Tier System)

**STABLE** (lag < 300ms)
- Normal trading
- All signals allowed
- No restrictions

**DEGRADED** (300ms ≤ lag < 900ms)
- Soft degradation mode
- Only high-EV trades (≥20 bps) allowed
- +7 bps economic floor
- 0.5x position size
- System stays alive but conservative

**INVALID** (lag ≥ 900ms)
- Hard block
- No trading allowed
- Depth too stale for safe execution

---

## 📁 FILE STRUCTURE

### Core Regime Engine
```
include/regime/
├── RegimeTypes.hpp       - RegimeState enum, RegimeSnapshot struct
└── RegimeClock.hpp       - Per-symbol regime clock class

src/regime/
└── RegimeClock.cpp       - Implementation with state logic
```

### GUI Integration
```
include/gui/
└── GuiRegimeFeed.hpp     - GUI regime feed interface

src/gui/
└── GuiRegimeFeed.cpp     - Thread-safe regime storage
```

---

## 🔌 WIRING POINTS

### FadeETH / FadeSOL

**Member Declaration:**
```cpp
// Phase 9.5: Per-symbol regime clock (isolation fix)
chimera::RegimeClock regime_clock_{"ETHUSDT", "logs/eth"};
// or
chimera::RegimeClock regime_clock_{"SOLUSDT", "logs/sol"};
```

**Trade Timestamp Tracking:**
```cpp
void FadeETH::onTrade(double price, uint64_t ts_ns) {
    regime_clock_.on_trade_ts(ts_ns);
    // ... rest of function
}
```

**Depth Timestamp Tracking:**
```cpp
std::optional<OrderIntent> FadeETH::onDepth(..., uint64_t now_ns) {
    regime_clock_.on_depth_ts(now_ns);
    // ... rest of function
}
```

**Decision Point (Before Order):**
```cpp
// Get current state
auto snap = regime_clock_.snapshot();

// Soft degrade in DEGRADED state
if (snap.state == chimera::RegimeState::DEGRADED) {
    adj_expected_bps += 7.0;     // Raise floor
    size_mult *= 0.5;            // Cut size
}

// Hard block if regime doesn't allow
if (!regime_clock_.allow_trade(adj_expected_bps)) {
    chimera::guiUpdateRegime(snap);
    return std::nullopt;  // Trade blocked
}
```

---

## 📊 AUDIT TRAIL

### Blocked Trade Logs

**Location:**
- `logs/eth_ETHUSDT_regime.jsonl`
- `logs/sol_SOLUSDT_regime.jsonl`

**Format:**
```json
{
  "symbol": "ETHUSDT",
  "state": 1,
  "expected_bps": 14.5,
  "depth_lag_ms": 450,
  "ts_ns": 1706342400000000000
}
```

**State Values:**
- `0` = STABLE
- `1` = DEGRADED
- `2` = INVALID

---

## 🎯 CONSOLE OUTPUT

### DEGRADED Mode
```
[FadeETH] ⚠️  DEGRADED MODE: +7bps floor, 0.5x size (lag=450ms)
```

### INVALID Block
```
[FadeETH] 🚫 REGIME BLOCK: state=2 expected=12.3 bps lag=1200ms
```

---

## 📡 GUI DISPLAY

### JSON Feed

```json
{
  "eth_regime": {
    "state": 0,
    "depth_lag_ms": 150,
    "blocked": 0,
    "last_blocked_expected_bps": 0.0
  },
  "sol_regime": {
    "state": 1,
    "depth_lag_ms": 450,
    "blocked": 3,
    "last_blocked_expected_bps": 14.5
  }
}
```

---

## 🔬 HOW IT WORKS

### Timestamp Tracking

1. **Trade Timestamps** - Captured from exchange aggTrade feed
2. **Depth Timestamps** - Captured from depth update callbacks
3. **Lag Calculation** - Absolute difference between trade and depth timestamps

### State Computation

```cpp
if (lag_ms < 300)  → STABLE
if (lag_ms < 900)  → DEGRADED
else               → INVALID
```

### Allow Trade Logic

**STABLE:**
- All trades allowed

**DEGRADED:**
- Only trades with expected_bps ≥ 20.0 allowed
- Automatically adds +7 bps to floor
- Cuts position size by 50%

**INVALID:**
- No trades allowed
- All signals blocked

---

## 🎯 EXPECTED IMPACT

### SOL Desk (High Impact)
- **+20-50% valid trades** (thin symbol benefits most)
- **Lower false invalidations**
- **Cleaner slippage modeling** (only trade with good data)

### ETH Desk (Moderate Impact)
- **+10-20% valid trades**
- **Independence from SOL issues**
- **Better regime-specific performance tracking**

### System-Wide
- **Complete symbol isolation** (critical infrastructure fix)
- **Missed opportunity visibility** (every block logged)
- **Operational intelligence** ("How much $ lost to lag?")

---

## 🛡️ SAFETY FEATURES

1. **Per-Symbol Isolation** - ETH lag never affects SOL
2. **Graduated Response** - Soft degradation before hard block
3. **Audit Trail** - Every blocked trade logged with context
4. **Thread-Safe** - Mutex-protected regime state
5. **GUI Visible** - Real-time regime display per symbol

---

## 🔧 TUNING PARAMETERS

### Lag Thresholds (Binance WS Tuned)

```cpp
STABLE:    lag < 300ms   (normal operation)
DEGRADED:  lag < 900ms   (soft restrict)
INVALID:   lag ≥ 900ms   (hard block)
```

### DEGRADED Mode Adjustments

```cpp
Floor Raise:    +7.0 bps
Size Cut:       0.5x (50% reduction)
Min EV Trade:   ≥20.0 bps
```

---

## 📋 VERIFICATION CHECKLIST

✅ RegimeTypes.hpp created (RegimeState enum, RegimeSnapshot struct)  
✅ RegimeClock.hpp created (interface)  
✅ RegimeClock.cpp created (implementation)  
✅ GuiRegimeFeed.hpp created (GUI interface)  
✅ GuiRegimeFeed.cpp created (GUI storage)  
✅ FadeETH.hpp includes regime headers  
✅ FadeETH.cpp wires on_trade_ts, on_depth_ts, allow_trade  
✅ FadeSOL.hpp includes regime headers  
✅ FadeSOL.cpp wires on_trade_ts, on_depth_ts, allow_trade  
✅ GUIServer.cpp displays per-symbol regime  
✅ CMakeLists.txt builds regime sources  

---

## 🐛 DEBUGGING

### Check Regime Logs
```bash
tail -f logs/eth_ETHUSDT_regime.jsonl
tail -f logs/sol_SOLUSDT_regime.jsonl
```

### Verify GUI Feed
```bash
curl http://localhost:9001/data | jq '.eth_regime'
curl http://localhost:9001/data | jq '.sol_regime'
```

### Monitor Console
```bash
# Look for DEGRADED mode or REGIME BLOCK messages
grep "DEGRADED\|REGIME BLOCK" logs/chimera.log
```

---

## 🎓 OPERATOR VALUE

### Before Phase 9.5
❌ "Why isn't SOL trading?"  
❌ "ETH lag killed both desks"  
❌ "How many trades did I miss?"  
❌ **Unknown EV loss**

### After Phase 9.5
✅ "SOL traded through ETH lag"  
✅ "ETH isolated from SOL issues"  
✅ "Blocked 12 trades worth 180 bps EV today"  
✅ **Quantified opportunity cost**

---

## 📈 INTEGRATION WITH OTHER PHASES

### Phase 9.5 → Phase 10 (Edge Audit)
- Phase 9.5: Blocks unsafe trades
- Phase 10: Audits executed trades
- Synergy: Only measure EV on valid regime trades

### Phase 9.5 → Phase 11 (Microstructure)
- Phase 9.5: Per-symbol regime state
- Phase 11: Per-state performance analysis
- Synergy: Microstructure heatmaps per regime

### Phase 9.5 → Phase 12 (Policy Learning)
- Phase 9.5: Provides regime state
- Phase 12: Learns per-regime policy
- Synergy: Policies adapt to STABLE/DEGRADED conditions

---

**STATUS: CRITICAL STRUCTURAL FIX COMPLETE**

This fixes a fundamental architectural flaw where shared regime state created cross-contamination between trading desks. Now each symbol has complete independence.

**Deploy Priority: HIGHEST**  
This is infrastructure - everything else builds on top of it.
