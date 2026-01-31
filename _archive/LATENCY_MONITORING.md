# EXCHANGE PROCESSING LATENCY - PRODUCTION SETUP

## ⚡ THE ONLY LATENCY THAT MATTERS

**What We Measure:**
```
Binance Server Timestamp → WebSocket Frame → VPS Receive → Chimera Parse
```

This is **Exchange Processing Latency** and includes:
- Network propagation delay
- Binance internal matching engine delay
- Gateway buffering
- WebSocket parsing overhead

This is THE latency that determines if your fades work or not.

## 🎯 TARGET LATENCY BY VPS LOCATION

| VPS Location | Expected p50 | Expected p99 | Status |
|--------------|--------------|--------------|---------|
| **AWS Tokyo** | 1-3 ms | < 8 ms | ✅ Excellent |
| **AWS Singapore** | 3-6 ms | < 12 ms | ✅ Good |
| **GCP Tokyo** | 2-4 ms | < 10 ms | ✅ Excellent |
| **Vultr Tokyo** | 4-8 ms | < 15 ms | ⚠️ Acceptable |
| **EU VPS** | 30-50 ms | < 80 ms | 🚫 Too slow for fades |
| **US East** | 15-25 ms | < 40 ms | ⚠️ Marginal |

**CRITICAL:** If your p99 > 25ms, **fades start losing edge**. You're trading stale prices.

## 🔧 STEP 1: CLOCK SYNCHRONIZATION (MANDATORY)

### Why This Matters

Your VPS clock MUST be accurate to within 0.5ms of true time. Otherwise, all latency measurements are garbage.

Binance sends timestamps in Unix milliseconds. You compare these to your local clock. If your clock is 5ms off, your "latency" measurement is worthless.

### Install and Configure Chrony

On your VPS (Ubuntu/Debian):

```bash
# Install chrony (better than ntpd for trading)
sudo apt update
sudo apt install chrony -y

# Enable and start
sudo systemctl enable chrony
sudo systemctl start chrony

# Check sync status
chronyc tracking
```

### Verify Clock Accuracy

**Good output:**
```
Reference ID    : A9FEA97B (time.google.com)
Stratum         : 3
Ref time (UTC)  : Mon Jan 27 06:15:23 2026
System time     : 0.000234567 seconds fast of NTP time
Last offset     : +0.000123456 seconds
RMS offset      : 0.000456789 seconds
Frequency       : 2.345 ppm slow
Residual freq   : +0.012 ppm
Skew            : 0.567 ppm
Root delay      : 0.012345678 seconds
Root dispersion : 0.000987654 seconds
Update interval : 64.5 seconds
Leap status     : Normal
```

**Key metric:** `System time : 0.000234567 seconds fast`
- **Target:** < 0.0005 seconds (0.5ms)
- **Acceptable:** < 0.001 seconds (1ms)
- **Bad:** > 0.005 seconds (5ms) → Fix before trading

### Troubleshoot Clock Sync

**If system time error > 1ms:**

```bash
# Force immediate sync
sudo chronyc -a makestep

# Check sources
chronyc sources -v

# Add better NTP servers (edit /etc/chrony/chrony.conf)
sudo nano /etc/chrony/chrony.conf
```

Add these lines for Asia-Pacific VPS:
```
server time.google.com iburst
server time.cloudflare.com iburst
server ntp.nict.jp iburst
server time.aws.com iburst
```

Restart:
```bash
sudo systemctl restart chrony
sleep 10
chronyc tracking
```

## 🚀 STEP 2: DEPLOY CHIMERA WITH LATENCY MONITORING

Chimera v4.3.2 includes production-grade latency monitoring via `LatencyProbe`.

### What You'll See

**Console Output (every 10th trade):**
```
[ETH_AGGTRADE #10] price=3245.67 qty=0.125 latency=1.2ms
[SOL_AGGTRADE #10] price=187.23 qty=5.2 latency=1.5ms
```

**Percentile Reports (every 30 seconds):**
```
[LATENCY_ETH] mean=1.8ms p50=1.5ms p90=3.2ms p99=5.8ms samples=512
[LATENCY_SOL] mean=2.1ms p50=1.9ms p90=3.8ms p99=6.3ms samples=512
```

**Degradation Warnings:**
```
⚠️  [ETH] LATENCY DEGRADED: p99=27.3ms (fades losing edge)
```

### GUI Dashboard

The latency displays in real-time:
```
ETH Desk:
  WS Latency: 1.45 ms

SOL Desk:
  WS Latency: 1.78 ms
```

## 📊 STEP 3: INTERPRET RESULTS

### Good Latency (Co-located VPS)

```
[LATENCY_ETH] mean=1.8ms p50=1.5ms p90=3.2ms p99=5.8ms
```

**What This Means:**
- **mean=1.8ms:** Average round-trip is excellent
- **p50=1.5ms:** Half of trades < 1.5ms (very fast)
- **p90=3.2ms:** 90% of trades < 3.2ms (good)
- **p99=5.8ms:** Worst 1% < 6ms (acceptable)

**Action:** ✅ You're good. Keep trading.

### Marginal Latency (Cloud VPS, wrong region)

```
[LATENCY_ETH] mean=18.5ms p50=15.2ms p90=28.7ms p99=45.3ms
```

**What This Means:**
- **mean=18.5ms:** Too slow for sub-bps fades
- **p99=45.3ms:** Worst case is unacceptable

**Action:** ⚠️ Reduce position sizes or migrate VPS closer to Binance.

### Bad Latency (Network issue or clock desync)

```
[LATENCY_ETH] mean=125.3ms p50=98.2ms p90=187.5ms p99=234.8ms
```

**What This Means:**
- **mean=125ms:** Either network issue or clock desync
- **p99=234ms:** You're trading 0.25 seconds stale

**Action:** 🚫 Stop trading immediately. Check:
1. `chronyc tracking` → Is clock synced?
2. `ping -c 10 stream.binance.com` → Network path quality?
3. VPS location → Are you in the wrong region?

## 🔬 STEP 4: ADVANCED MONITORING

### Baseline Your VPS

Run Chimera for 1 hour and record:
```bash
# Watch percentile reports
tail -f chimera.log | grep LATENCY
```

Your **baseline p99** is the number you care about. Track this daily.

### Alert Thresholds

Set up monitoring for:

1. **p99 spike:** If p99 > 2× baseline for > 5 minutes
   - Example: Baseline p99 = 5ms, alert if > 10ms
   - Action: Check network, consider reducing size

2. **Latency = 0ms:** Calculation broke or clock desync
   - Action: Restart Chimera, check chrony

3. **Latency > 100ms:** Major issue
   - Action: Stop trading, investigate

### Compare to Network RTT (Health Check)

```bash
# Baseline network latency
ping -i 0.2 -c 100 stream.binance.com | tail -3
```

Example output:
```
--- stream.binance.com ping statistics ---
100 packets transmitted, 100 received, 0% packet loss
rtt min/avg/max/mdev = 1.234/1.567/2.345/0.123 ms
```

Your **exchange processing latency** should be ~2-5ms higher than `rtt avg`.

If processing latency is 10× network RTT, something is wrong (check CPU load).

## ⚙️ STEP 5: LATENCY-AWARE TRADING (FUTURE)

### Adaptive Sizing Based on p99

Add to FadeETH/FadeSOL Config:

```cpp
// If latency degrades, reduce sizing
if (latency_probe.p99_ms() > 20) {
    size_mult *= 0.5;  // Half size
}

if (latency_probe.p99_ms() > 40) {
    return std::nullopt;  // Disable trading
}
```

**Why:** At high latency, you're trading stale prices. Microstructure signals decay.

### Gate Trades on Recent Latency

```cpp
// Don't trade if last 3 trades had high latency
if (latency_probe.recent_high_latency(3, 15.0)) {
    std::cout << "[GATE] Latency spike detected, skipping trade" << std::endl;
    return std::nullopt;
}
```

## 🎯 PRODUCTION CHECKLIST

Before going live:

- [ ] chrony installed and running
- [ ] `chronyc tracking` shows error < 0.5ms
- [ ] Chimera shows non-zero latency within 30 seconds
- [ ] p50 latency < 5ms (for co-located VPS)
- [ ] p99 latency < 15ms (for co-located VPS)
- [ ] No "LATENCY DEGRADED" warnings
- [ ] GUI dashboard shows real-time latency
- [ ] Baseline p99 recorded for monitoring

## 📈 WHAT GOOD LOOKS LIKE

**VPS: AWS Tokyo (ap-northeast-1)**
```
[LATENCY_ETH] mean=1.5ms p50=1.2ms p90=2.8ms p99=4.5ms samples=512
[LATENCY_SOL] mean=1.8ms p50=1.5ms p90=3.2ms p99=5.2ms samples=512
```

**VPS: Vultr Singapore**
```
[LATENCY_ETH] mean=6.5ms p50=5.8ms p90=9.2ms p99=14.3ms samples=512
[LATENCY_SOL] mean=7.2ms p50=6.5ms p90=10.1ms p99=15.8ms samples=512
```

Both are tradeable. Tokyo is better.

## 🚫 WHAT BAD LOOKS LIKE

**Clock desync detected:**
```
[LATENCY_ETH] mean=0.0ms p50=0.0ms p90=0.0ms p99=0.0ms samples=0
```
Action: Check `chronyc tracking`, restart Chimera

**Network degradation:**
```
[LATENCY_ETH] mean=45.2ms p50=38.7ms p90=67.3ms p99=98.5ms samples=512
⚠️  [ETH] LATENCY DEGRADED: p99=98.5ms (fades losing edge)
```
Action: Check `ping stream.binance.com`, contact VPS provider

## 📞 TROUBLESHOOTING QUICK REFERENCE

| Problem | Check | Fix |
|---------|-------|-----|
| Latency = 0.00ms | `chronyc tracking` | `sudo chronyc -a makestep` |
| Latency > 100ms | `chronyc tracking`, then `ping stream.binance.com` | Fix clock or migrate VPS |
| p99 > 25ms | VPS location | Migrate to Tokyo/Singapore |
| Spiky latency | CPU load: `top` | Reduce other processes |
| No samples | Chimera running? | Check if WebSockets connected |

---
**Version:** v4.3.2
**Requires:** chrony, properly configured VPS clock
**Target:** p99 < 15ms for co-located, < 25ms for cloud VPS
