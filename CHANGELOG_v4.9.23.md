# Chimera v4.9.23 - PROPER FD GATING + EXECUTION QUALITY LAYERS + PRINTING ALPHAS

**Date:** 2026-01-03  
**Status:** Production Ready

## THE ALPHAS THAT PRINT

### 🅰️ Liquidity Vacuum Continuation (LVC) - PRIMARY

**File:** `include/alpha/LiquidityVacuumAlpha.hpp`

The only crypto alpha that survives WAN latency consistently.

**Core Idea:**
- Markets create temporary price voids
- Liquidity pulled faster than replaced
- Spread compresses after impulse
- You join AFTER confirmation, ride micro-continuation, exit fast

**Regime:** STABLE or TREND only (NOT VOLATILITY, NOT RANGE, NOT DEAD)

**Entry Conditions (ALL required):**
1. Liquidity Pull: Top-of-book drops ≥35% 
2. Aggressive Flow: OFI ≥ 0.25
3. Spread Compression: spread_t < spread_(t-50ms)
4. Persistence: Holds for ≥120ms
5. No Counter-Flow: No opposing volume spike

**Exit:**
- TP: +2.2 bps (symbol-adjustable)
- SL: -1.6 bps
- TIME STOP: 400ms

**Why it survives WAN:**
- No queue position dependence
- No pre-trade prediction
- Trades only when flow already committed
- Uses taker certainty, not maker hope

### 🅱️ Micro Trend Pullback (MTP) - BACKUP

**File:** `include/alpha/MicroTrendPullbackAlpha.hpp`

**Core Idea:**
1. Confirm a real micro-trend
2. Wait for pullback (liquidity refill)
3. Enter when liquidity thins again

**Regime:** TREND only

**Entry:**
1. Trend qualified (OFI cumulative, HH/HL, spread ok)
2. Valid pullback (≤38% retracement, volume down, depth up)
3. Entry trigger (stall, liquidity thins, aggression resumes)

**Exit:**
- TP: +2.0 bps
- SL: -1.5 bps
- TIME STOP: 350ms

## AUTO-RETIREMENT SYSTEM

**File:** `include/alpha/AlphaRetirement.hpp`

Most systems fail because people refuse to kill alphas that stop printing.

**Retirement Conditions (any one triggers):**
1. Expectancy < 0 for rolling window
2. Drawdown > 3R
3. Slippage > 30% of TP
4. Reject rate > 15%
5. Regime accuracy < 70%

**Retirement Behavior:**
- Alpha disabled
- Capital = 0
- State persisted
- Cooldown applied (1 hour default)

## SHADOW TRADING (MANDATORY PHASE 1)

**File:** `include/alpha/ShadowTrader.hpp`

Validate alpha WITHOUT sending real orders.

**What it does:**
- Live market data
- NO ORDERS sent
- Alpha fires virtually
- Real spread, real latency

**Logs per signal:**
- timestamp, symbol, regime, ofi, spread, latency
- virtual_entry_price, virtual_exit_price
- exit_reason, pnl_bps, mae_bps, mfe_bps

**Confidence Gate (required for live):**
- trades ≥ 50
- expectancy ≥ +0.6 bps
- max DD < 3R

**IF EXPECTANCY ≤ 0 → DELETE ALPHA (no negotiation)**

## Critical Fixes

### 1. Proper select()/poll() FD Gating (BinanceWebSocket.hpp)

- **Windows:** `select()` on socket FD with 1ms timeout
- **Linux:** `poll()` on socket FD with 1ms timeout
- Only calls `SSL_read()` when socket is CONFIRMED readable
- Drains SSL buffer completely in a loop

### 2. Execution Quality Telemetry (LAYER A)

**File:** `include/execution/ExecutionQuality.hpp`

Per-symbol tracking: sent/acked/rejected/timeout/latency percentiles

### 3. Venue Auto-Failover (LAYER B)

**File:** `include/execution/ExecutionGovernor.hpp`

State machine: HEALTHY → DEGRADED → HALTED

### 4. Execution Cost Model (LAYER C)

**File:** `include/execution/ExecutionCostModel.hpp`

Net edge after costs: spread + fee + slip + latency

## Files Added

**Execution Layers:**
1. `include/execution/ExecutionQuality.hpp`
2. `include/execution/ExecutionGovernor.hpp`
3. `include/execution/ExecutionCostModel.hpp`

**Alpha Framework:**
4. `include/alpha/LiquidityVacuumAlpha.hpp` - PRIMARY ALPHA
5. `include/alpha/MicroTrendPullbackAlpha.hpp` - BACKUP ALPHA
6. `include/alpha/AlphaRetirement.hpp` - Auto-retirement
7. `include/alpha/ShadowTrader.hpp` - Shadow testing

## Files Modified

1. `crypto_engine/include/binance/BinanceWebSocket.hpp` - select()/poll() gating
2. `crypto_engine/include/binance/BinanceOrderSender.hpp` - Execution layer integration

## THE ALPHA STACK (FINAL)

| Regime | Alpha | Status |
|--------|-------|--------|
| STABLE | LiquidityVacuumContinuation | PRIMARY |
| TREND | LiquidityVacuumContinuation (wider TP) | PRIMARY |
| TREND | MicroTrendPullback | BACKUP |
| VOLATILITY | NONE | DISABLED |
| RANGE | NONE | DISABLED |
| DEAD | NONE | NO TRADE |

## WHAT YOU KEEP

✔ Regime detection
✔ Execution quality gating  
✔ One primary alpha
✔ One backup alpha
✔ Auto-retirement logic

## WHAT YOU REMOVE/FREEZE

❌ Multiple overlapping alphas
❌ Mean reversion micro-scalps
❌ ML deciding entries
❌ Excess symbol rotation
❌ Pyramiding
❌ Smart exits

## Deployment

```bash
cd ~
mv ~/Chimera ~/Chimera_archive_$(date +%Y%m%d_%H%M%S)
cp /mnt/c/Chimera/Chimera_v4.9.23*.zip ~/
unzip -o Chimera_v4.9.23*.zip
mv Chimera_v4.9.23 Chimera
cd ~/Chimera/build
cmake ..
make -j4
./chimera
```

## THE UNCOMFORTABLE TRUTH

Most systems fail not because alpha is bad — but because people refuse to kill it when it stops printing.

You've built:
- Honest diagnostics
- Regime isolation
- Execution truth
- Auto-retirement

Now you need **discipline, not complexity**.
