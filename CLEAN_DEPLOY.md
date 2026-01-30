# CHIMERA v4.6.3 - CLEAN DEPLOYMENT GUIDE

**Package:** Chimera_v4_6_3_FINAL.tar.gz  
**Size:** 72 KB  
**Checksum:** ab880a5909979cd3a9aa9c83f3c01321  
**Executable:** `./build/chimera` (NOT chimera_real)

---

## 🚨 CRITICAL - CLEAN DEPLOYMENT SEQUENCE

These commands ensure a **clean installation** with no leftover files from old versions.

```bash
# 1. SCP package to VPS
scp -i ~/.ssh/ChimeraKey.pem ~/Downloads/Chimera_v4_6_3_FINAL.tar.gz ubuntu@56.155.82.45:~/

# 2. SSH to VPS
ssh -i ~/.ssh/ChimeraKey.pem ubuntu@56.155.82.45

# 3. Stop old process (if running)
pkill chimera

# 4. Clean deployment (archive old, extract fresh)
cd ~
[ -d Chimera ] && mv Chimera Chimera_archive_$(date +%Y%m%d_%H%M%S)
tar xzf Chimera_v4_6_3_FINAL.tar.gz

# 5. Verify clean extraction
cd ~/Chimera
pwd  # Should show: /home/ubuntu/Chimera
ls   # Should show: CMakeLists.txt, include/, src/, build/, logs/, etc.

# 6. Build (clean build directory first)
mkdir -p logs
cd build
rm -rf *
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j2

# 7. Verify executable name
ls -la chimera  # Should exist (NOT chimera_real)

# 8. Run
cd ~/Chimera
./build/chimera

# 9. Test graceful shutdown
# Press CTRL+C - should exit in < 100ms
```

---

## ✅ WHAT'S INCLUDED

**Complete System:**
- Phase 9.5: Per-symbol regime clocks (ETH lag can't blind SOL)
- Phase 10: Edge audit and EV tracking
- Phase 11: Microstructure analysis
- Phase 12: Auto-policy learning (self-optimizing parameters)
- Graceful shutdown (CTRL+C works)
- Proper executable name (`chimera`)

---

## 🔧 EXECUTABLE NAME FIXED

**Before:** `./build/chimera_real`  
**After:** `./build/chimera`

**Changed in CMakeLists.txt:**
```cmake
project(Chimera)
add_executable(chimera ${SOURCES})
target_link_libraries(chimera ...)
```

**This name will NOT change again.**

---

## 📋 VERIFICATION CHECKLIST

After deployment, verify:

```bash
# 1. Correct directory
pwd
# Should output: /home/ubuntu/Chimera

# 2. Executable exists with correct name
ls -la ~/Chimera/build/chimera
# Should exist (NOT chimera_real)

# 3. Clean build (no old .o files)
find ~/Chimera/build -name "*.o" | head -5
# Should show recently compiled .o files with today's timestamp

# 4. Process running
ps aux | grep chimera
# Should show: ./build/chimera

# 5. GUI accessible
curl http://localhost:9001/data | jq '.eth_regime'
# Should return regime JSON

# 6. Logs being written
ls -lh ~/Chimera/logs/
# Should show recent .jsonl files

# 7. Graceful shutdown works
# CTRL+C the process
# Should exit cleanly in < 100ms
```

---

## 🐛 TROUBLESHOOTING

### "No such file or directory: chimera_real"

You're trying to run the old executable name.

**Fix:**
```bash
cd ~/Chimera
./build/chimera  # NOT chimera_real
```

### "Building from wrong directory"

You're in an archive directory.

**Fix:**
```bash
cd ~
pwd  # Make sure you're in /home/ubuntu
cd Chimera  # NOT Chimera_archive_*
```

### "Compilation errors about expected_bps or latency_ms"

You extracted over an old version without cleaning.

**Fix:**
```bash
cd ~
rm -rf Chimera
tar xzf Chimera_v4_6_3_FINAL.tar.gz
cd Chimera/build && rm -rf * && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j2
```

### "Process won't stop with CTRL+C"

You're running an old version.

**Fix:**
```bash
pkill -9 chimera
cd ~/Chimera
./build/chimera  # Run new version
```

---

## 🔄 SYSTEMD SERVICE (OPTIONAL)

If you want auto-restart on crash/reboot:

```bash
sudo tee /etc/systemd/system/chimera.service > /dev/null <<EOF
[Unit]
Description=Chimera HFT Trading System
After=network.target

[Service]
Type=simple
User=ubuntu
WorkingDirectory=/home/ubuntu/Chimera
ExecStart=/home/ubuntu/Chimera/build/chimera
Restart=always
RestartSec=5
StandardOutput=append:/home/ubuntu/Chimera/logs/chimera.log
StandardError=append:/home/ubuntu/Chimera/logs/chimera_error.log

[Install]
WantedBy=multi-user.target
EOF

# Enable and start
sudo systemctl daemon-reload
sudo systemctl enable chimera
sudo systemctl start chimera

# Check status
sudo systemctl status chimera

# View logs
sudo journalctl -u chimera -f

# Stop
sudo systemctl stop chimera
```

---

## 📊 POST-DEPLOYMENT VERIFICATION

After system is running, verify all phases are working:

### Phase 9.5 (Regime Clocks)
```bash
# Check regime state
curl http://localhost:9001/data | jq '.eth_regime, .sol_regime'

# Check regime logs
tail -5 ~/Chimera/logs/eth_ETHUSDT_regime.jsonl
tail -5 ~/Chimera/logs/sol_SOLUSDT_regime.jsonl
```

### Phase 10 (Edge Audit)
```bash
# Check edge EMA
curl http://localhost:9001/data | jq '.eth.economic_throttle, .sol.economic_throttle'

# Check edge logs
tail -5 ~/Chimera/logs/eth_ETHUSDT_edge.jsonl
tail -5 ~/Chimera/logs/sol_SOLUSDT_edge.jsonl
```

### Phase 11 (Microstructure)
```bash
# Check microstructure logs
tail -5 ~/Chimera/logs/eth_ETHUSDT_micro.jsonl
tail -5 ~/Chimera/logs/sol_SOLUSDT_micro.jsonl
```

### Phase 12 (Policy Learning)
```bash
# Check learned policies
curl http://localhost:9001/data | jq '.eth_policy, .sol_policy'

# Check policy logs
tail -5 ~/Chimera/logs/eth_ETHUSDT_policy.jsonl
tail -5 ~/Chimera/logs/sol_SOLUSDT_policy.jsonl
```

---

## 🎯 EXPECTED CONSOLE OUTPUT

```
[CHIMERA] Starting...
[DEPTH_WS] ✅ Connected to /ws/ethusdt@depth@100ms
[DEPTH_WS] ✅ Connected to /ws/solusdt@depth@100ms
[AGGTRADE_WS] ✅ Connected to /ws/ethusdt@aggTrade
[AGGTRADE_WS] ✅ Connected to /ws/solusdt@aggTrade
[GUI] Server listening on port 9001

✅ ONLINE - Waiting for signals...

[STATUS] ETH: AggTrades=1234 Depth=5678 | SOL: AggTrades=890 Depth=1234

[FadeETH] 🔥 SIGNAL GENERATED (ECONOMICALLY APPROVED)
  Expected Move: 14.5 bps
  Net Edge: 11.2 bps (after 8.3 bps costs)
  Dynamic Floor: 10.0 bps
  TP: 18.0 bps (dynamic)
  SL: 11.7 bps (0.65x TP)
  Size Mult: 1.2
  Notional: $600
  Side: SELL

[ETH] 📊 Policy Updated - MinEdge: 12.5 TP: 1.02x SL: 0.66x Size: 1.08x

# CTRL+C

[CHIMERA] Shutdown signal received (signal 2)
[CHIMERA] Initiating graceful shutdown...
[CHIMERA] Tick loop stopped
[SHUTDOWN] Initiating graceful shutdown...
[SHUTDOWN] Stopping GUI server...
[SHUTDOWN] GUI stopped
[SHUTDOWN] Stopping depth WebSocket...
[DEPTH_WS] Clean exit: ETHUSDT
[SHUTDOWN] Depth WebSocket stopped
[SHUTDOWN] Stopping aggTrade WebSocket...
[AGGTRADE_WS] Stopped: ETHUSDT
[AGGTRADE_WS] Clean exit: ETHUSDT
[SHUTDOWN] AggTrade WebSocket stopped
[SHUTDOWN] Stopping SOL WebSockets...
[DEPTH_WS] Clean exit: SOLUSDT
[AGGTRADE_WS] Stopped: SOLUSDT
[AGGTRADE_WS] Clean exit: SOLUSDT
[SHUTDOWN] SOL WebSockets stopped
[SHUTDOWN] Final counts:
  ETH: AggTrades=1234 Depth=5678
  SOL: AggTrades=890 Depth=1234
[SHUTDOWN] ✅ Clean exit
[CHIMERA] Shutdown complete
```

---

## ⚠️ COMMON MISTAKES TO AVOID

1. **Don't run from archive directory**
   - ❌ `cd Chimera_archive_20260127_*`
   - ✅ `cd Chimera`

2. **Don't use old executable name**
   - ❌ `./build/chimera_real`
   - ✅ `./build/chimera`

3. **Don't skip clean build**
   - ❌ `make -j2` (incremental)
   - ✅ `rm -rf * && cmake .. && make -j2` (clean)

4. **Don't extract over old version**
   - ❌ `tar xzf ...` (without moving old Chimera)
   - ✅ `mv Chimera Chimera_archive_* && tar xzf ...`

---

## 📝 ONE-LINE DEPLOYMENT (COPY/PASTE)

```bash
cd ~ && [ -d Chimera ] && mv Chimera Chimera_archive_$(date +%Y%m%d_%H%M%S) && tar xzf Chimera_v4_6_3_FINAL.tar.gz && cd Chimera && mkdir -p logs && cd build && rm -rf * && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j2 && cd ~/Chimera && ./build/chimera
```

---

**DEPLOY THIS VERSION**  
**Token Usage:** 127,564 / 190,000 (67.1% used)

Clean deployment guaranteed.
