# Chimera v4.4.4 - GUI Edge Telemetry

## What Was Added

### New Telemetry Fields (Both ETH & SOL)

**In ChimeraTelemetry struct:**
- `expected_edge_bps` - Expected move before trade
- `realized_edge_bps` - Actual PnL after close
- `cost_bps` - Dynamic economic floor
- `ev_leak_bps` - Expected vs realized gap
- `slippage_bps` - Average slippage from fills
- `capture_ratio` - Realized/Expected ratio
- `last_fill_ts` - Last fill timestamp
- `economic_throttle` - If cost floor blocking trades

---

## GUI Dashboard Updates

### New Cards Added

**ETH EDGE TRACKING** (6 metrics)
**SOL EDGE TRACKING** (6 metrics)

### Displayed Metrics
- Expected Edge (bps before entry)
- Realized Edge (bps after exit)
- Capture Ratio (0.0-1.0, target >0.7)
- Avg Slippage (execution cost)
- Cost Floor (dynamic minimum)
- Throttle Status (ON/OFF)

---

## Engine Integration

**Modified Files:**
- `src/FadeETH.cpp` - Updates telemetry on trade close
- `src/FadeSOL.cpp` - Updates telemetry on trade close
- `include/GUIServer.hpp` - Added telemetry fields
- `src/GUIServer.cpp` - JSON + HTML updates

**When Triggered:**
Every time `edge_tracker.onClose()` is called, the GUI telemetry updates with:
- Latest realized edge
- Updated capture ratio (rolling window)
- Current avg slippage
- Dynamic cost floor

---

## What You'll See

### ETH Desk Example
```
ETH EDGE TRACKING
Expected Edge:    14.2 bps
Realized Edge:    10.8 bps
Capture Ratio:    0.76
Avg Slippage:     1.8 bps
Cost Floor:       10.5 bps
Throttle:         OFF
```

### SOL Desk Example
```
SOL EDGE TRACKING
Expected Edge:    22.5 bps
Realized Edge:    18.3 bps
Capture Ratio:    0.81
Avg Slippage:     2.1 bps
Cost Floor:       12.0 bps
Throttle:         OFF
```

---

## Access Dashboard

**URL:** `http://VPS_IP:8080`

**Update Rate:** 500ms polling

**What Good Looks Like:**
- Capture Ratio: > 0.70
- Slippage: < 3 bps
- Throttle: OFF (most of the time)
- Cost Floor: 10-12 bps stable

**Warning Signs:**
- Capture Ratio: < 0.50 (execution problems)
- Slippage: > 5 bps (latency or liquidity issues)
- Throttle: ON constantly (conditions too poor)
- Cost Floor: > 15 bps (adaptive protection active)

---

## Testing

**After deployment:**

1. Open browser to `http://VPS_IP:8080`
2. Verify all cards load
3. Watch for first trade
4. Confirm edge metrics populate after close

**Console should show:**
```
[FadeETH] 🏁 POSITION CLOSED: WIN | PnL: 14.2 bps
  Capture Ratio: 0.76 | Avg Slip: 1.8 bps
```

**GUI should immediately reflect:**
- Realized Edge: 14.2 bps
- Capture Ratio: 0.76
- Avg Slippage: 1.8 bps

---

## Deploy

```bash
scp -i ~/.ssh/ChimeraKey.pem ~/Downloads/Chimera_v4.4.4.tar.gz ubuntu@56.155.82.45:~/
ssh -i ~/.ssh/ChimeraKey.pem ubuntu@56.155.82.45
mv ~/Chimera ~/Chimera_old
tar xzf Chimera_v4.4.4.tar.gz
cd ~/Chimera/build && rm -rf * && cmake .. && make -j2
cd ~/Chimera && ./build/chimera_real
```

Then open browser: `http://56.155.82.45:8080`

---

## Technical Details

**JSON Structure:**
```json
{
  "eth": {
    "expected_edge_bps": 14.2,
    "realized_edge_bps": 10.8,
    "cost_bps": 10.5,
    "capture_ratio": 0.76,
    "slippage_bps": 1.8,
    "economic_throttle": false,
    "last_fill_ts": 1706342400000
  },
  "sol": { ... }
}
```

**Update Flow:**
1. Trade closes → `edge_tracker.onClose(pnl, ts)`
2. EdgeLeakTracker computes metrics
3. Engine updates `telemetry_->realized_edge_bps` etc.
4. GUIServer serializes to JSON
5. Dashboard polls `/data` every 500ms
6. JavaScript updates DOM

---

## What This Unlocks

**Real-time Edge Monitoring:**
- See if system is capturing expected edge
- Identify slippage creep before it kills PnL
- Monitor dynamic cost floor adaptation
- Detect when economic throttle engages

**Operational Decisions:**
- If capture < 0.6: Check VPS latency
- If slippage > 4bps: Market conditions poor
- If cost floor > 15bps: System protecting capital
- If throttle ON: Wait for better conditions

---

**Checksum:** `d7752ca8a0d2b7395807ab6b87cc1b44`  
**Size:** 56KB

All v4.4.0-4.4.3 features intact + GUI telemetry layer.
