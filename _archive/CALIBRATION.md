# Chimera v4.4.3 - Live Calibration

## Applied Parameters

### ETH (Tight Microstructure)
```
OFI Z Min:        0.75 → 0.6      (more sensitive)
Impulse Floor:    0.08 → 0.05 bps (sub-bps moves)
Impulse Ceiling:  3.5  → 1.5  bps (tighter range)
TP Max:           30   → 28   bps
Weights:          OFI=0.5, IMP=0.35, VOL=0.15
Size Range:       0.5x - 2.0x
DD Limit:         -60 bps
Cooldown:         180s
```

### SOL (Momentum/Chaos)
```
OFI Z Min:        0.65 → 0.85     bps (filter noise)
Impulse Floor:    0.06 → 0.25     bps (real moves only)
TP Min/Max:       12/28 → 15/40   bps (wider targets)
Weights:          OFI=0.45, IMP=0.30, VOL=0.25
Size Range:       0.5x - 1.5x
DD Limit:         -40 bps (tighter)
Stall Timeout:    600 → 300ms     (faster)
Cooldown:         300s
```

---

## Expected Behavior

**ETH:**
- Trades: 40-120/day
- Avg: +4-8 bps net
- More frequent, smaller moves
- OFI-driven entries

**SOL:**
- Trades: 10-30/day
- Avg: +15-30 bps net
- Less frequent, larger moves
- Higher conviction only

---

## What Changed From v4.4.2

**File:** `src/main.cpp` (config section only)

1. ETH more sensitive (lower thresholds)
2. SOL more selective (higher thresholds)
3. Better weight distribution
4. Adaptive sizing 0.5x-2.0x (was 0.25x-1.5x)
5. Updated DD limits and cooldowns

---

## Deploy

```bash
scp -i ~/.ssh/ChimeraKey.pem ~/Downloads/Chimera_v4.4.3.tar.gz ubuntu@56.155.82.45:~/
ssh -i ~/.ssh/ChimeraKey.pem ubuntu@56.155.82.45
mv ~/Chimera ~/Chimera_v4.4.2_$(date +%Y%m%d_%H%M%S)
tar xzf Chimera_v4.4.3.tar.gz
cd ~/Chimera/build && rm -rf * && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j2
cd ~/Chimera && ./build/chimera_real
```

---

## Watch For

**Good Signs:**
- `[FadeETH] 🔥 SIGNAL GENERATED` when OFI > 1.0, Impulse > 0.08
- `[FadeSOL] 🔥 SIGNAL GENERATED` when OFI > 1.5, Impulse > 0.3
- TP hits at 15-25 bps
- Regime = STABLE or TRANSITION

**Expected Rejections:**
- `[ECON] 🚫 BLOCKED: Net edge below economic floor` (this is GOOD - protecting capital)
- Small impulses rejected early

**Red Flags:**
- No trades for > 1 hour during active market
- Regime stuck INVALID
- All trades hitting SL

---

## Tuning If Needed

**If too few trades:**
- Lower `ofi_z_minimum` by 0.05
- Lower `impulse_floor_bps` by 20%

**If too many losses:**
- Raise `ofi_z_minimum` by 0.1
- Raise economic floor check

**Never:**
- Set TP_Min < 12 bps (below cost floor)
- Disable economic gate

---

**Checksum:** `d7a879cd0773f03e45c5d9e51e100973`  
**Size:** 54KB  
**All v4.4.0-4.4.2 fixes intact**
