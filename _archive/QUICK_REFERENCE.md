# CHIMERA v4.3.2 - QUICK REFERENCE CARD

## 🔥 ONE-LINE SUMMARY
Cost-calibrated dual-desk fade system with 10bps economic floor and explicit Binance fee accounting.

## ⚡ DEPLOYMENT (3 COMMANDS)
```bash
cd ~ && mv Chimera Chimera_backup && tar xzf CHIMERA_v4_3_2_COST_CALIBRATED.tar.gz
cd ~/Chimera/build && rm -rf * && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j2
cd ~/Chimera && ./build/chimera_real
```

## 💰 ECONOMIC FLOOR (NEVER CHANGE)
- **10.0 bps** net edge after all costs
- **12.0 bps** minimum expected move
- **Binance fees:** 5bps entry + 3bps exit + 1bps spread + 2bps slippage = 11bps total

## 🎯 KEY PARAMETERS

### ETH Desk
```
Notional:  $2K - $25K (base $8K)
TP:        12-30 bps (dynamic)
SL:        0.65x TP
Min Edge:  1.2 (adaptive: 0.5-1.2)
Killswitch: 3 losses OR -50bps DD
```

### SOL Desk
```
Notional:  $1K - $12K (base $4K)
TP:        12-28 bps (dynamic)
SL:        0.7x TP (tighter)
Min Edge:  1.0 (adaptive: 0.5-1.0)
Killswitch: 2 losses OR -35bps DD
```

## 📊 EXPECTED PERFORMANCE

### ETH
- Trades/day: 40-120 (many rejected)
- Net/trade: 3-9 bps after costs
- Daily: 80-250 bps (clean regime)

### SOL
- Trades/day: 60-180
- Net/trade: 4-12 bps after costs
- Daily: 120-300 bps (clean regime)

## ✅ SUCCESS INDICATORS
- [ ] Rejection messages appear (normal, 60-80% rejection rate is GOOD)
- [ ] All approved trades show net edge > 10bps
- [ ] Dynamic TP always >= 12bps
- [ ] No negative PnL streaks > killswitch threshold
- [ ] GUI shows both ETH and SOL cards

## 🚫 NEVER DO THIS
1. Reduce ECON_FLOOR_BPS below 10.0 → fee donation
2. Reduce MIN_EXPECTED_MOVE_BPS below 10.0 → structural losses
3. Disable economic gate "temporarily" → not optional
4. Trust backtests without fee modeling → live costs higher
5. Panic when 80% rejection rate → economic gate working correctly

## 📞 EMERGENCY ROLLBACK
```bash
pkill -f chimera_real
cd ~ && mv Chimera Chimera_broken && mv Chimera_backup Chimera
cd ~/Chimera && ./build/chimera_real
```

## 🔬 FILES MODIFIED FROM v4.3.1
- EconomicsGovernor.hpp (NEW)
- FadeETH.hpp (UPDATED)
- FadeETH.cpp (UPDATED)
- FadeSOL.hpp (UPDATED)
- FadeSOL.cpp (UPDATED)

## 📈 ECONOMIC GATE LOGIC
```
1. expected_move = max(ofi_pred, impulse_pred, vol_pred)
2. fee_cost = 5 + 3 + spread + 2
3. net_edge = expected_move - fee_cost
4. REJECT if net_edge < 10.0 OR expected_move < 12.0
5. APPROVE otherwise → generate order with dynamic TP/SL
```

## 🎛️ GUI ACCESS
```bash
# From local machine:
ssh -N -L 9001:localhost:9001 chimera

# Then open: http://localhost:9001
```

## 📋 LOG PATTERNS

### Normal Operation:
```
[ECON] 🚫 ETHUSDT BLOCKED: Net edge below economic floor
[ECON] ✅ SOLUSDT APPROVED
[FadeETH] 🔥 SIGNAL GENERATED (ECONOMICALLY APPROVED)
[FadeSOL] 📍 POSITION OPENED: BUY 22.5 @ 187.23
[FadeETH] 🏁 POSITION CLOSED: WIN | PnL: 14.2 bps
```

### Killswitch Triggered:
```
[FadeSOL] 🛑 KILLSWITCH: 2 consecutive losses
```

### Daily Reset:
```
[FadeETH] Daily PnL reset: +142.5 bps yesterday
```

## 🔧 TUNING (IF NEEDED)

### Too many rejections (>90%):
1. Check if markets are simply quiet (normal)
2. If persistent, verify predictions are working
3. Don't reduce economic floor

### Too few rejections (<20%):
1. Verify economic gate is being called
2. Check log for "ECONOMICALLY APPROVED" messages
3. Should see mix of approvals and rejections

### Frequent killswitch:
1. Markets in bad regime (wait)
2. Consider increasing max_consecutive_losses (ETH: 3→4, SOL: 2→3)
3. Don't disable killswitch

---
**Version:** v4.3.2
**Status:** Production-ready
**Doc:** See DEPLOY_README.md for full details
