# CHANGELOG v4.9.7 - FILL CALLBACK FIX + TAKER ROUTING

**Date:** 2026-01-02  
**Status:** Production - Fixes 2-trade deadlock bug

---

## THE BUG

**Symptom:**
- Only 2 trades in 17 hours
- Dashboard shows: State=RANGING, Intent=NO_TRADE, Reason=STABLE/FLAT
- W/L counter stuck at 0/0
- CSV only shows ENTRY records, no EXIT records

**Root Cause:**
```
MAKER_FIRST routing + No fill callback = DEADLOCK

1. enter() called → pending_fill_ = true, state stays FLAT
2. Entry logged (PnL=0) ✓
3. Every tick: if (pending_fill_) return; → DEAD LOOP
4. onFill() never called → Never transitions to PROBE
5. Trade stuck forever, engine appears dead
```

**Why Dashboard Showed NO_TRADE:**
```cpp
// Dashboard Intent is NO_TRADE unless in PROBE/CONFIRM
TradeIntent dash_intent = TradeIntent::NO_TRADE;
if (ms_state == MicroState::PROBE) dash_intent = MOMENTUM;    // Never reached!
if (ms_state == MicroState::CONFIRM) dash_intent = MOMENTUM;  // Never reached!
```

---

## THE FIX

### 1. External Fill Callback (BinanceEngine.hpp)

```cpp
// NEW: External callback type for MicroScalp integration
using ExternalFillCallback = std::function<void(
    const char* symbol, bool is_buy, double qty, double price)>;

// Added to BinanceEngine:
void setExternalFillCallback(ExternalFillCallback cb);

// on_fill() now calls:
if (external_fill_callback_) {
    external_fill_callback_(sym_name, side == Side::Buy, qty, price);
}
```

### 2. Fill Callback Wiring (main_triple.cpp)

```cpp
// Wire fill callbacks from BinanceEngine to MicroScalp engines
binance_engine.setExternalFillCallback([&](const char* symbol, bool is_buy, double qty, double price) {
    if (strcmp(symbol, "BTCUSDT") == 0) microscalp_btc.onFill(FillType::TAKER, price);
    else if (strcmp(symbol, "ETHUSDT") == 0) microscalp_eth.onFill(FillType::TAKER, price);
    else if (strcmp(symbol, "SOLUSDT") == 0) microscalp_sol.onFill(FillType::TAKER, price);
});
```

### 3. Default Routing Changed (CryptoMicroScalp.hpp)

```cpp
// Changed from MAKER_FIRST to TAKER_ONLY as safety fallback
RoutingMode routing_mode_ = RoutingMode::TAKER_ONLY;

// TAKER_ONLY flow (no deadlock):
// enter() → state = PROBE immediately → normal lifecycle
```

### 4. Explicit Routing Mode Set (main_triple.cpp)

```cpp
// Set TAKER_ONLY explicitly (belt and suspenders)
microscalp_btc.setRoutingMode(RoutingMode::TAKER_ONLY);
microscalp_eth.setRoutingMode(RoutingMode::TAKER_ONLY);
microscalp_sol.setRoutingMode(RoutingMode::TAKER_ONLY);
```

---

## FLOW COMPARISON

### Before (v4.9.6 - BROKEN)
```
enter() → pending_fill_=true, state=FLAT
      ↓
onTick() → if(pending_fill_) return;  ← STUCK HERE FOREVER
      ↓
onFill() NEVER CALLED (no callback wired)
      ↓
Dashboard: STABLE/FLAT, NO_TRADE
```

### After (v4.9.7 - FIXED)
```
enter() → state=PROBE immediately (TAKER_ONLY)
      ↓
onTick() → handleProbe() → handleConfirm() → exit()
      ↓
Full lifecycle works, Dashboard: TRENDING/MOMENTUM
```

---

## FILES CHANGED

| File | Change |
|------|--------|
| `crypto_engine/include/binance/BinanceEngine.hpp` | Added ExternalFillCallback, setExternalFillCallback(), call in on_fill() |
| `crypto_engine/include/microscalp/CryptoMicroScalp.hpp` | Changed default routing to TAKER_ONLY |
| `src/main_triple.cpp` | Added fill callback wiring, explicit TAKER_ONLY routing, cstring include |

---

## EXPECTED BEHAVIOR

After this fix:
- Trades will enter AND exit properly
- W/L counter will increment
- CSV will show both ENTRY and EXIT records
- Dashboard will show MOMENTUM intent when trading
- 3-state lifecycle (FLAT → PROBE → CONFIRM) works as designed

---

## WHY SO FEW TRADES BEFORE?

Two compounding issues:

1. **Edge calculation was too conservative** (fixed in v4.9.6)
   - Old: OFI 0.8 → 1.2 bps edge → BLOCKED (< 4.5 bps cost)
   - New: OFI 0.8 → 6+ bps edge → TRADES

2. **Fill callback deadlock** (fixed in v4.9.7)
   - Even when edge passed, trades got stuck in FLAT
   - Engine appeared to work but never exited trades

Both fixes together = proper trading should resume.

---

## DEPLOYMENT

```bash
# On VPS (45.85.3.38)
cd ~
mv Chimera Chimera_archive_$(date +%Y%m%d_%H%M%S)
cp /mnt/c/Chimera/chimera_v4_9_7.zip ~/
unzip chimera_v4_9_7.zip
mv chimera_src Chimera
cd ~/Chimera && mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release && make -j4
cd .. && ./build/chimera
```

---

## VERIFICATION

1. Check log shows `→ PROBE` after entry (not stuck in FLAT)
2. Check dashboard shows MOMENTUM intent during trades
3. W/L counter should increment after trades close
4. CSV should show both BUY entries and SELL exits
5. Trade flow: FLAT → PROBE → CONFIRM → EXIT → FLAT

---

## GIT

```bash
git add -A
git commit -m "v4.9.7: Fix fill callback deadlock + TAKER routing"
git tag v4.9.7
git push origin main --tags
```
