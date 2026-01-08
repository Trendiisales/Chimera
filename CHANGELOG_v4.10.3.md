# CHANGELOG v4.10.3 - GOLD ARCHITECTURE CLEANUP

## DATE: 2025-01-06

## SUMMARY
**Old Gold engine DELETED. New 3-part Gold architecture finalized.**

---

## ❌ DELETED: Old GoldCampaignEngine.hpp

The old Gold engine (`include/engines/GoldCampaignEngine.hpp`) has been **permanently deleted** because it violated the Gold architecture audit:

| Violation | Details |
|-----------|---------|
| ❌ Had entry logic | `executeEntry()` method placed trades directly |
| ❌ Had position management | `managePosition()` with trailing stops |
| ❌ Had risk calculations | Computed size internally |
| ❌ No portfolio authority | Could trade without asking portfolio |
| ❌ Mixed responsibilities | Combined campaign detection + trading |

A tombstone file (`GoldCampaignEngine.hpp.DELETED`) documents why it was removed.

---

## ✅ NEW GOLD SYSTEM (3-Part Architecture)

### 1. GoldCampaignEngine_v2_1.hpp (Campaign Detection ONLY)
```
Location: include/engines/GoldCampaignEngine_v2_1.hpp
Namespace: gold::
```
- **HTF regime** mandatory (NEUTRAL = no trade)
- **OBSERVING** gated by structural proximity (within $15 or 0.5×H1_range of HTF midpoint)
- **Minimum 45 minutes** observing + ≥2 failed counter-probes to activate
- **Campaign max lifetime**: 360 minutes
- **Hard invalidation** on structure break
- **NEVER places trades** - produces `CampaignContext` only

### 2. GoldEngine_v5_2.hpp (Thin Executor)
```
Location: include/engines/GoldEngine_v5_2.hpp
Namespace: gold::
```
- **Requires portfolio permission** via `PermissionCallback`
- **Requires active campaign** (CampaignState::ACTIVE)
- **Entry** within 50% of campaign range from key level
- **Stops** structural (beyond observe_high/low + $2 buffer)
- **Partial** at +1.5R (exit 60%, move stop to BE+$4)
- **Runner trails** after 3R at 60% of campaign range
- **Campaign invalidation = immediate full exit**
- **Max 1 trade per day**

### 3. GoldScaleGuard.hpp (Discipline-Based Scaling)
```
Location: include/portfolio/GoldScaleGuard.hpp
Namespace: portfolio::
```
| Level | Risk | Requirements |
|-------|------|--------------|
| MICRO | 0.10% | Default, proving ground |
| LEVEL_1 | 0.20% | After 30 trades with metrics passing |
| LEVEL_2 | 0.30% | After 60 trades with continued discipline |

**Metrics required:**
- 90%+ trades from ACTIVE campaign
- 0 runners turning into losers
- 70%+ losses are soft (≤ -0.6R)
- 60%+ winners take partials

### 4. GoldExecutionContract.hpp (Truthful Tagging)
```
Location: include/engines/GoldExecutionContract.hpp
Namespace: gold::
```
- Validates every Gold trade report
- Catches invalid states (runner_failed with positive PnL)
- Required for scale guard accuracy

### 5. PortfolioModeController.hpp (SINGLE AUTHORITY)
```
Location: include/portfolio/PortfolioModeController.hpp
Function: canTradeGold()
```
- **THE ONLY** permission gate for Gold
- Blocks Gold when indices active (HARD BLOCK)
- Blocks Gold without active campaign
- Returns risk from scale guard
- Portfolio decides, engines request

---

## 🔒 GOLD PERMISSION RULES (Enforced in Code)

| Rule | Implementation |
|------|----------------|
| HTF regime mandatory | Campaign rejects NEUTRAL |
| Campaign ACTIVE required | Engine checks before entry |
| Index priority | Portfolio blocks during index trades |
| Scale earned | GoldScaleGuard controls risk |
| One trade per campaign | MAX_TRADES_DAY = 1 |

---

## 📊 EXPECTED LOG SEQUENCE

```
[GOLD-CAMPAIGN] HTF Regime: NEUTRAL -> LONG
[GOLD-CAMPAIGN] OBSERVING started: dir=LONG htf_mid=2650.00
[GOLD-CAMPAIGN] ACTIVE: key=2648.50 conf=0.74 range=12.50 time=45m fails=2
[PORTFOLIO] GOLD_PERMISSION=DENIED reason=INDEX_ACTIVE
... (index position closes) ...
[PORTFOLIO] GOLD_PERMISSION=GRANTED risk=0.10% reason=MICRO_ONLY
[GOLD-V5.2] ENTRY LONG @ 2650.00 stop=2635.00 size=0.03 risk=$100 conf=0.74
[GOLD-V5.2] PARTIAL @ 2665.00: exited 0.02 lots, runner=0.01, new_stop=2654.00
[GOLD-V5.2] Trail: stop 2654.00 -> 2667.00 (R=4.25)
[GOLD-V5.2] EXIT LONG @ 2665.50 reason=STOP size=0.01 pnl=$155.00 (5.17R)
[GOLD-SCALE] Trade recorded: pnl=5.17R campaign=YES partial=YES runner_fail=NO
```

---

## 🚨 GOLD STATUS: DISABLED

Gold is **compile-ready but NOT wired** in CfdEngineIntegration.hpp.

To enable Gold:
1. Register XAUUSD symbol in CfdEngineIntegration
2. Route H1/M5 ticks to GoldEngineV5_2
3. Wire permission callback to PortfolioModeController::canTradeGold()
4. Test in shadow mode first

**Current micro-live symbols:** NAS100, US30 only

---

## FILES CHANGED

```
DELETED:
  include/engines/GoldCampaignEngine.hpp

CREATED:
  include/engines/GoldCampaignEngine.hpp.DELETED (tombstone)

VERIFIED INTACT:
  include/engines/GoldCampaignEngine_v2_1.hpp
  include/engines/GoldEngine_v5_2.hpp
  include/engines/GoldExecutionContract.hpp
  include/portfolio/GoldScaleGuard.hpp
  include/portfolio/PortfolioModeController.hpp
```

---

## BUILD

```bash
# Archive first
mv ~/Chimera ~/Chimera_archive_$(date +%Y%m%d_%H%M%S)

# Deploy
cp /mnt/c/Chimera/Chimera.zip ~/ && cd ~ && unzip -o Chimera.zip
cd ~/Chimera/build && cmake .. -DUSE_OPENAPI=ON && make chimera -j4

# Run (micro-live, NAS100 + US30 only)
./chimera
```
