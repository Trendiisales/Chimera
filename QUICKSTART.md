# Chimera Production - Quick Start Guide

## 30-Second Setup

```bash
# 1. Set credentials
export FIX_USERNAME="live.blackbull.8077780"
export FIX_PASSWORD="8077780"

# 2. Build
cd chimera_production
./deploy.sh

# 3. Run (shadow mode)
cd build && ./chimera
```

**That's it.** System will connect to FIX, run 4 engines, and expose telemetry on port 8080.

---

## What Just Happened?

### System Started With:
- ✅ 4 Active Trading Engines (Structural, Compression, Cascade, Micro)
- ✅ FIX Connection to live-uk-eqx-01.p.c-trader.com
- ✅ Shadow Mode ENABLED (no live orders)
- ✅ Telemetry HTTP server on :8080
- ✅ Thread isolation (cores 0-3)

### What You'll See:

```
============================================================
  CHIMERA PRODUCTION - Unified Trading System
============================================================
  Engines:    4 Active (Structural, Compression, Cascade, Micro)
  FIX:        live-uk-eqx-01.p.c-trader.com
  Symbols:    XAUUSD, XAGUSD (Isolated)
  Mode:       SHADOW (Orders Blocked)
  Telemetry:  HTTP :8080
  Cores:      FIX:0, XAU:1, XAG:2, Telemetry:3
============================================================

[V2DESK] Initialized with 4 engines:
  - StructuralMomentumEngine
  - CompressionBreakEngine
  - StopCascadeEngine
  - MicroImpulseEngine

[FIX] Connected to live-uk-eqx-01.p.c-trader.com:5211
[FIX] Sent Logon (seq=1)
[FIX] Requested MarketData for XAUUSD (seq=2)
[FIX] Requested MarketData for XAGUSD (seq=3)
[FIX] Reader thread started on core 0
[XAU] Engine thread started on core 1
[XAG] Engine thread started on core 2
[TELEMETRY] HTTP server started on port 8080 (core 3)

[SYSTEM] All threads started. Monitoring active...

[STATUS] PnL: 0.00 | Float: 0.00 | Open: 0 | XAU RTT: 5.2ms | XAG RTT: 6.1ms
```

---

## Monitor the System

### View Telemetry (Real-time JSON)

```bash
# In another terminal
curl http://localhost:8080 | jq
```

**Example Response:**
```json
{
  "timestamp": 1708012345678,
  "shadow_mode": true,
  "fix_connected": true,
  "portfolio": {
    "daily_pnl": 12.45,
    "floating_pnl": -3.20,
    "total_open": 2
  },
  "xauusd": {
    "connected": true,
    "rtt_ms": 5.2,
    "realized": 10.50,
    "daily_loss": -5.00
  },
  "xagusd": {
    "connected": true,
    "rtt_ms": 6.1,
    "realized": 1.95,
    "daily_loss": -2.30
  }
}
```

### Watch Live Updates

```bash
# Continuous monitoring
watch -n 1 'curl -s http://localhost:8080 | jq'
```

---

## Common Commands

### Start System
```bash
cd chimera_production/build
./chimera
```

### Stop System
```bash
# Press Ctrl+C for graceful shutdown
# Or from another terminal:
killall chimera
```

### Rebuild After Changes
```bash
cd chimera_production/build
make -j$(nproc)
```

### Check System Status
```bash
# CPU usage
top -p $(pidof chimera)

# Memory usage
ps aux | grep chimera

# Network connections
netstat -an | grep -E '5211|5212|8080'

# Logs
tail -f chimera.log
```

---

## Important Files

| File | Purpose | When to Edit |
|------|---------|--------------|
| `main.cpp` | System core | To change shadow mode or add features |
| `config/V2Config.hpp` | Risk limits | To adjust capital or risk parameters |
| `core/V2Desk.hpp` | Engine coordinator | To add/remove active engines |
| `deploy.sh` | Deployment script | Never (unless adding deploy steps) |

---

## Safety Checklist (Before Live)

- [ ] System ran 48+ hours in shadow mode
- [ ] No crashes or memory leaks
- [ ] RTT < 10ms average
- [ ] All 4 engines generated proposals
- [ ] Daily loss limit triggers correctly
- [ ] PnL calculations verified
- [ ] You understand how to stop the system
- [ ] You have monitoring alerts configured

**See `VERIFICATION.md` for complete checklist.**

---

## Troubleshooting

### "FIX credentials not set"
```bash
export FIX_USERNAME="live.blackbull.8077780"
export FIX_PASSWORD="8077780"
```

Add to `~/.bashrc` for persistence.

### "Failed to connect QUOTE session"
- Check network: `ping live-uk-eqx-01.p.c-trader.com`
- Verify firewall: `sudo ufw allow 5211`
- Check credentials are correct

### "Port 8080 already in use"
```bash
# Find what's using it
sudo lsof -i :8080

# Kill it
sudo kill -9 <PID>
```

### High Latency (RTT > 25ms)
- Check VPS location (should be near Equinix)
- Disable unnecessary services
- Verify core pinning: `ps -eLo pid,tid,psr,comm | grep chimera`

### Engines Not Trading
- System is in shadow mode (expected)
- Check capital limits: `curl localhost:8080 | jq '.xauusd.daily_loss'`
- Verify execution governor allows trading

---

## Next Steps

### 1. Shadow Testing (48 hours minimum)
Let system run and verify:
- Engines generate proposals
- PnL tracking works
- Risk limits function
- System is stable

### 2. Review Engine Behavior
```bash
# Monitor telemetry
watch -n 1 'curl -s localhost:8080 | jq ".portfolio.daily_pnl"'

# Check logs
tail -f build/chimera.log
```

### 3. Adjust Risk Parameters (if needed)
Edit `config/V2Config.hpp`:
```cpp
constexpr double DAILY_MAX_LOSS = 200.0;  // Adjust
constexpr double LOT_SIZE = 0.01;         // Adjust
constexpr int MAX_CONCURRENT_TOTAL = 4;   // Adjust
```

Rebuild: `cd build && make`

### 4. Add More Engines (optional)
See `INTEGRATION_MANIFEST.md` for 18 additional engines available.

### 5. Enable Live Trading (CAUTION)
**Only after 48+ hours shadow testing:**
1. Stop system
2. Edit `main.cpp` line ~465: `shadow_gate.set_shadow(false);`
3. Rebuild
4. Start and monitor constantly

---

## Key Metrics to Watch

| Metric | Healthy Range | Alert If |
|--------|---------------|----------|
| RTT | 2-10 ms | > 25 ms |
| Daily PnL | -50 to +100 NZD | < -200 NZD |
| Open Positions | 0-4 | > 4 |
| Memory Usage | < 100 MB | > 500 MB |
| CPU Usage | < 50% | > 80% |

---

## Emergency Procedures

### If System Goes Haywire:
1. **Press `Ctrl+C`** (graceful shutdown)
2. If frozen: `killall -9 chimera`
3. Log into broker and **manually close positions**
4. Review logs: `tail -n 1000 build/chimera.log`

### If Losing Money Fast:
1. Stop system immediately
2. Close all positions manually via broker
3. Review `VERIFICATION.md` - did you skip any steps?
4. Restart in shadow mode and investigate

---

## Resources

- **README.md** - Complete system documentation
- **VERIFICATION.md** - Pre-live checklist (MUST READ)
- **INTEGRATION_MANIFEST.md** - Proof all engines preserved
- **ENGINES_MANIFEST.md** (in baseline) - All 22 engines catalog

---

## Support

### Self-Help
1. Check console logs
2. Review telemetry: `curl localhost:8080`
3. Verify FIX credentials
4. Test in shadow mode

### Documentation
- Read `README.md` for architecture
- Read `VERIFICATION.md` before going live
- Check `INTEGRATION_MANIFEST.md` for engine details

### Emergency
- Stop system immediately
- Close positions manually
- Review what went wrong
- Do NOT re-enable until root cause found

---

## Remember

> **This system manages real money.**
> 
> - Start with minimal capital
> - Test in shadow mode extensively
> - Monitor constantly when live
> - Accept that losses will occur
> - Know how to stop the system instantly

**No guarantees. Trade at your own risk.**

---

Happy Trading! 🚀

(But seriously, test in shadow mode first.)

---

Last Updated: 2025-02-15  
Version: 1.0.0
