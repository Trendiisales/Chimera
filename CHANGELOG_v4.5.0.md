# Chimera v4.5.0 - Engine-Level Symbol Ownership + Official Crypto Ruleset

**Release Date**: 2024-12-30

## 🔴 ROOT CAUSE FIXED

The v4.4.0 trades that appeared on XAUUSD were NOT from IncomeEngine.
They were from the CFD engine's PureScalper, which still had XAUUSD enabled.

**Problem**: No mechanism existed to prevent different engines from trading the same symbol.
**Solution**: Engine-level symbol ownership enforcement with engine ID propagation.

---

## 🧠 NEW: Official Crypto Engine Trading Ruleset

Added comprehensive crypto trading ruleset with strict controls:

### Key Principles
- **DISABLED BY DEFAULT** - Requires explicit enable + 1 week shadow validation
- **EPISODIC ALPHA ONLY** - Not general-purpose trading
- **COMPLETE ISOLATION** - Cannot affect Income/CFD engines
- **FIXED RISK** - No dynamic sizing, no scaling, no martingale

### Symbol Scope (STRICT)
- BTCUSDT and ETHUSDT **ONLY**
- No other symbols. No exceptions.

### Global Activation Gates (ALL MUST PASS)

| Gate | Name | Thresholds |
|------|------|------------|
| G1 | Infrastructure/Speed | Median RTT ≤ 0.5ms, P99 RTT ≤ 1.2ms, No packet loss |
| G2 | Market Quality | Spread ≤ 1.5× session median, Min depth, No crossed books |
| G3 | Volatility | Short-term vol ≤ cap, No impulse candles (3σ) |
| G4 | Cross-Asset Stress | Crypto stress < 0.7, Equity stress < 0.6, Income exposure < 50% |
| G5 | Self-Discipline | Daily PnL > -$50, Loss streak < 2, Trades < 5/session |

### Allowed Trade Classes

**CLASS A: Liquidity Vacuum Capture (PRIMARY)**
- Triggers: 70%+ depth drop within 50ms, spread widens, flow pauses 20ms+
- Entry: Follow last aggressive flow with marketable limit
- Exit: +3 ticks TP, -5 ticks SL, 750ms hard timeout
- No pyramiding. No averaging.

**CLASS B: Momentum Continuation (SECONDARY)**
- Only if Class A inactive
- Triggers: Book imbalance persists 300ms+, trade flow aligns, spread stable
- Entry: Late entry, fixed clip size
- Exit: +2 ticks TP, -4 ticks SL, 1.5s timeout

**DISALLOWED (ABSOLUTE)**
- Mean reversion fades
- News spikes
- Multi-level scaling
- Martingale
- Holding > 2 seconds
- Any trade without timeout

### Position & Risk Rules
- Fixed size per symbol (BTC: 0.001, ETH: 0.01)
- Max 1 position per symbol
- Max 2 trades per minute
- Max 5 trades per session
- $5 max loss per trade
- 60s cooldown after loss
- 2 losses = engine OFF

### Files Added
- `crypto_engine/include/CryptoRuleset.hpp` - Complete ruleset implementation

---

## 🔧 FIX: Crypto GUI Display

**Problem**: Crypto prices were not updating in GUI because BinanceEngine lacked a tick callback.

**Solution**: Added `setTickCallback()` to BinanceEngine that fires on every price update.

```cpp
// BinanceEngine.hpp
using TickCallback = std::function<void(const char* symbol, double bid, double ask, 
                                        double bid_qty, double ask_qty, double latency_ms)>;

void setTickCallback(TickCallback cb) {
    tick_callback_ = std::move(cb);
}

// Called in dispatcher_loop when processing bookTicker (fastest stream)
if (tick_callback_) {
    tick_callback_(symbol, ticker.best_bid, ticker.best_ask,
                   ticker.best_bid_qty, ticker.best_ask_qty, latency_ms);
}
```

Now wired in main_triple.cpp to update GUI:
```cpp
binance_engine.setTickCallback([](const char* symbol, double bid, double ask,
                                  double bid_qty, double ask_qty, double latency_ms) {
    g_gui.updateSymbolTick(symbol, bid, ask, latency_ms);
    // Also update microstructure display
    g_gui.updateMicro(imbalance, vpin, pressure, spread, bid, ask, symbol);
});
```

---

## 🔑 THE KEY FIX: Engine ID Propagation

The previous implementation had a critical wiring bug:
- `submitOrder()` was hardcoded to `CFD_ALPHA` 
- **Every order, no matter where it came from, was evaluated as CFD_ALPHA**
- IncomeEngine filtering was "correct" but enforcement never saw `EngineId::INCOME`

**The fix**: 

1. `submitOrder()` now accepts an `EngineId` parameter from the caller
2. The old signature is **DELETED** to prevent accidental use:

```cpp
// ❌ DELETED - compiler error if anyone tries this
void submitOrder(const char*, int8_t, double, double, double = 0.0) = delete;

// ✅ ONLY valid signature - requires EngineId
void submitOrder(EngineId engine_id, const char* symbol, int8_t side, 
                 double qty, double price, double pnl = 0.0);
```

This is a **compile-time enforcement** - no runtime bugs, no defaults, no globals.

---

## ⚙️ Design Principles

### 1. DENY-BY-DEFAULT
If no explicit ownership exists → **BLOCK**
- Unknown engine = all symbols blocked
- Unconfigured symbol = blocked
- Avoids footguns when new engines/symbols are added

### 2. MODE-AWARE ENFORCEMENT
- **DEMO mode**: Log + block (visibility during testing) - DEFAULT
- **LIVE mode**: Throw/abort (guarantees during production)

### 3. ENGINE ID PROPAGATION
- Each engine passes its own `EngineId` when submitting orders
- Ownership enforcement uses the passed engine ID, not a hardcoded value
- Enables proper trade attribution

### 4. NAMING CLARITY
- Renamed `CFD_ALPHA` → `CFD` (there is no "Alpha" system in Chimera)
- CFD engine covers PureScalper + micro strategies
- Future: Consider splitting into `CFD_SCALPER` and `CFD_MICRO`

---

## ⚙️ Changes

### 1. EngineOwnership.hpp (`include/core/EngineOwnership.hpp`)

Complete engine-level symbol isolation system:

```cpp
enum class EngineId : uint8_t {
    UNKNOWN = 0,
    BINANCE = 1,      // Crypto engine
    CFD = 2,          // CFD engine (PureScalper, micros)
    INCOME = 3,       // Income engine (NAS100 only)
    SHADOW = 4        // Shadow execution
};

// DENY-BY-DEFAULT behavior
bool isAllowed(EngineId engine, const std::string& symbol) {
    if (engine == EngineId::UNKNOWN) return false;        // DENY
    auto it = allowed_.find(engine);
    if (it == allowed_.end()) return false;               // DENY - not configured
    return it->second.count(symbol) > 0;                  // DENY if not in list
}

// Mode-aware enforcement
bool assertAllowed(EngineId engine, const std::string& symbol) {
    if (isAllowed(engine, symbol)) return true;
    
    if (enforcement_mode_ == EnforcementMode::LIVE) {
        throw std::runtime_error("...");  // FATAL
    } else {
        return false;  // DEMO: log + block
    }
}
```

### 2. CfdEngine.submitOrder() - NOW REQUIRES ENGINE ID

```cpp
// OLD SIGNATURE IS DELETED - compile error if used
void submitOrder(const char*, int8_t, double, double, double = 0.0) = delete;

// v4.5.0: Engine ID must be passed by caller
inline void submitOrder(Chimera::EngineId engine_id, const char* symbol, 
                        int8_t side, double qty, double price, double pnl = 0.0) {
    // Enforce ownership using the PASSED engine ID, not hardcoded
    if (!Chimera::EngineOwnership::instance().isAllowedWithLog(engine_id, symbol)) {
        std::cout << "[ENGINE-BLOCK] engine=" << Chimera::engine_id_str(engine_id)
                  << " symbol=" << symbol << " BLOCKED\n";
        return;
    }
    // ... rest of order submission
}
```

**Compile-time enforcement**: Anyone who tries to call `submitOrder(symbol, side, ...)` 
without an `EngineId` gets a compiler error, not a runtime bug.

### 3. PureScalper Call Site - PASSES CFD ENGINE ID

```cpp
// In processTick():
submitOrder(Chimera::EngineId::CFD, tick.symbol, scalp.direction, 
            scalp.size, price, trade_pnl);
```

### 4. IncomeEngine - USES INCOME ENGINE ID

```cpp
void execute_entry(...) {
    // Defense-in-depth: check ownership with INCOME engine ID
    if (!Chimera::EngineOwnership::instance().isAllowed(
            Chimera::EngineId::INCOME, symbol)) {
        log("[INCOME][ENGINE-BLOCK] Entry BLOCKED - not in allowed list");
        return;
    }
    // ... rest of entry logic
}
```

### 5. logShadowTradeCSV - NOW REQUIRES ENGINE ID

```cpp
// v4.5.0: Engine ID required for attribution
void logShadowTradeCSV(Chimera::EngineId engine_id, const char* symbol, ...);

// Call site:
logShadowTradeCSV(Chimera::EngineId::CFD, tick.symbol, ...);
```

---

## 🔧 Deployment

### Build Commands (VPS WSL)
```bash
cd ~
rm -rf chimera_src
unzip -o /mnt/c/Chimera/Chimera_v4_5_0.zip
cd chimera_src
mkdir build && cd build
cmake ..
make -j4 chimera
./chimera
```

### Verification

On startup:
```
[ENGINE-OWNERSHIP] Initialized with DENY-BY-DEFAULT policy
[ENGINE-OWNERSHIP] Current Configuration:
  Enforcement mode: DEMO (log+block)
  Policy: DENY-BY-DEFAULT (unconfigured engine+symbol = BLOCKED)
  INCOME: NAS100
  CFD: XAUUSD XAGUSD US30 SPX500 GER40 UK100 EURUSD GBPUSD ...
  BINANCE: BTCUSDT ETHUSDT SOLUSDT AVAXUSDT ...
  Violation count: 0
```

Blocked trades will log with correct engine ID:
```
[ENGINE-BLOCK] engine=INCOME symbol=XAUUSD BLOCKED - not in allowed list
```

Successful trades will log with attribution:
```
[EXEC_QUEUED] engine=CFD symbol=XAUUSD BUY 0.01 @ 2650.50
[INCOME] engine=INCOME ENTRY NAS100 LONG size=0.0100 price=21450.50
```

---

## 🧪 VERIFICATION TEST

After deploying:

1. Disable PureScalper
2. Run IncomeEngine  
3. Try to trade XAUUSD

**Expected result**:
```
[ENGINE-BLOCK] engine=INCOME symbol=XAUUSD BLOCKED - not in allowed list
```

If you see that, ownership is finally real.

---

## 🛡️ Safety Guarantees

1. **DENY-BY-DEFAULT**: Unconfigured = blocked
2. **ENGINE ID PROPAGATION**: Caller specifies identity, not hardcoded
3. **Mode-Aware**: Demo logs, Live throws
4. **Defense in Depth**: Multiple checking layers
5. **Audit Trail**: All violations logged with engine ID
6. **Runtime Configuration**: Can adjust ownership without recompile

---

## 📋 Files Changed

| File | Change |
|------|--------|
| `include/core/EngineOwnership.hpp` | Engine ownership with deny-by-default, restricted BINANCE to BTCUSDT/ETHUSDT |
| `crypto_engine/include/CryptoRuleset.hpp` | **NEW** - Complete crypto trading ruleset |
| `crypto_engine/include/binance/BinanceEngine.hpp` | Added `setTickCallback()` for GUI updates |
| `cfd_engine/include/CfdEngine.hpp` | submitOrder now accepts engine_id |
| `income_engine/include/IncomeEngine.hpp` | Defense-in-depth checks |
| `src/main_triple.cpp` | Initialize ownership, crypto ruleset, tick callback wiring |
| `config/symbol_profiles.ini` | Updated comments |

---

## 🔍 Diagnostic Commands

```cpp
// Print current configuration
Chimera::EngineOwnership::instance().printConfig();

// Check violation count
uint64_t violations = Chimera::EngineOwnership::instance().getViolationCount();

// Set live mode (for production)
Chimera::EngineOwnership::instance().setEnforcementMode(Chimera::EnforcementMode::LIVE);

// Check if specific engine+symbol is allowed
bool ok = Chimera::EngineOwnership::instance().isAllowed(
    Chimera::EngineId::INCOME, "NAS100");  // true
bool bad = Chimera::EngineOwnership::instance().isAllowed(
    Chimera::EngineId::INCOME, "XAUUSD");  // false
```

---

## ✅ Verification Checklist

After deployment, verify:

- [ ] Startup shows DENY-BY-DEFAULT policy message
- [ ] Enforcement mode is DEMO for shadow testing
- [ ] NAS100 trades only appear with `engine=INCOME`
- [ ] XAUUSD trades only appear with `engine=CFD`
- [ ] Violations are logged with correct engine ID
- [ ] Violation count increments on blocked trades

---

## 🚨 Breaking Changes

**API Change**: `CfdEngine::submitOrder()` now requires `EngineId` as first parameter.

Old:
```cpp
submitOrder(symbol, side, qty, price);
```

New:
```cpp
submitOrder(Chimera::EngineId::CFD, symbol, side, qty, price);
```

All internal call sites have been updated.
