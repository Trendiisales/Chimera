# Chimera v6.66 - ML Ready + Trade Blotter + Opportunity Alerts

## Release Date: December 23, 2025

## v6.66 Changes
- **NEW**: Trade Blotter panel showing recent trades with time/symbol/side/qty/price
- **NEW**: Opportunity Alerts panel - fires when conviction ≥ 6 with MOMENTUM/REVERSION intent
- **NEW**: ML Feature Logger status panel (placeholder for future ML integration)
- **NEW**: state_gated counter displayed in Core Metrics
- **FIXED**: Baseline volatility was 0.0005 causing vol_z = 12000+, now starts at 5.0
- **FIXED**: Market State panel layout - cleaner 2-column grid with dedicated reason box
- **IMPROVED**: Size multiplier shown (0.0x to 2.0x based on conviction)
- **IMPROVED**: 4-column dashboard layout for more information density

## Architecture Notes
The dashboard is now ready for ML integration:
- Features being captured: OFI, VPIN, spread, vol_z, trend, momentum, regime
- Trade outcomes can be labeled for supervised learning
- Opportunity alerts show which setups triggered

---

# Chimera v6.65 - Crypto Feed + Performance Fix

## Summary
This release completes the market state machine integration with:
- MarketState wired into CfdEngine tick loop
- State-driven strategy gating (blocks trades when intent != allowed)
- Conviction-scaled position sizing
- Config persistence to disk
- Dashboard save button

---

## ✅ COMPLETED FEATURES

### 1. MarketState Integration in CfdEngine
- `stateClassifier_.classify()` called once per tick in processTick()
- Result stored in `currentState_` for strategies to read
- Market state broadcasted to GUI via `marketStateCallback_`
- Baseline spread/vol tracked via EMA for normalization

### 2. State-Driven Strategy Gating
```cpp
// Early exit if NO_TRADE
if (mktState.intent == TradeIntent::NO_TRADE) {
    stats_.state_gated.fetch_add(1);
    return;
}

// Intent alignment check
if (mktState.intent == MOMENTUM && mktState.state != TRENDING) {
    intentAligned = false;
}
```
New stat: `state_gated` tracks trades blocked by market state

### 3. Conviction-Scaled Position Sizing
```cpp
double sizeMultiplier = mktState.sizeMultiplier();  // 0.0 to 2.0
double adjustedSize = baseSize * decision.riskMultiplier * sizeMultiplier;
```
- SKIP: 0.0x (no trade)
- LOW: 0.5x
- NORMAL: 1.0x
- HIGH: 1.5x
- A_PLUS: 2.0x

### 4. Config Persistence
- `TradingConfig::saveToFile("chimera_config.json")`
- `TradingConfig::loadFromFile("chimera_config.json")`
- Auto-loads at startup in main_dual.cpp
- Dashboard "SAVE TO DISK" button triggers save

### 5. Dashboard Updates
- Split APPLY/SAVE buttons
- Market State panel displays live state/intent/conviction
- Save confirmation visual feedback

---

## 📁 FILES CHANGED

| File | Changes |
|------|---------|
| `cfd_engine/include/CfdEngine.hpp` | MarketState classifier integration, state gating |
| `include/shared/MarketState.hpp` | NEW - State machine types and classifier |
| `include/shared/TradingConfig.hpp` | Added saveToFile/loadFromFile |
| `include/gui/GUIBroadcaster.hpp` | save_config/reload_config commands |
| `src/main_dual.cpp` | Config load at startup, marketStateCallback |
| `chimera_dashboard.html` | Save button, market state display |

---

## 🚀 VPS Deploy

```bash
cd ~ && rm -rf Chimera
unzip /mnt/c/Chimera/Chimera_v6.64_COMPLETE.zip
cd Chimera/build && cmake .. && make -j8
./chimera
```

Dashboard: Open `chimera_dashboard.html`, connect to `ws://45.85.3.38:7777`

Config is now persisted in `chimera_config.json` in the working directory.
