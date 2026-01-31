# CHIMERA v4.12.0 - QUICK VERIFICATION CHECKLIST

## PRE-DEPLOYMENT CHECKS
- [ ] Archive existing Chimera: `mv ~/Chimera ~/Chimera_archive_$(date +%Y%m%d_%H%M%S)`
- [ ] Tarball uploaded to VPS: `ls -lh ~/Chimera_v4_12_0_DECISION_VISIBILITY_PATCH.tar.gz`
- [ ] Systemctl service stopped: `sudo systemctl stop chimera`

## BUILD CHECKS
- [ ] Clean build directory: `cd ~/Chimera/build && rm -rf *`
- [ ] CMake successful: `cmake .. -DCMAKE_BUILD_TYPE=Release`
- [ ] Make successful: `make -j2`
- [ ] See "[100%] Built target chimera"

## POST-DEPLOYMENT CHECKS (Within 5 Minutes)
- [ ] Service started: `sudo systemctl start chimera`
- [ ] Service running: `sudo systemctl status chimera` shows "active (running)"
- [ ] Log file exists: `ls -lh ~/Chimera/logs/chimera.log`
- [ ] WebSocket connected: grep "WebSocket" in logs
- [ ] Depth updates flowing: grep "depth" in logs

## DECISION PIPELINE CHECKS (Within 10 Minutes)
- [ ] Decision emitted: `grep "[DECISION]" ~/Chimera/logs/chimera.log`
- [ ] Router accepted: `grep "DECISION ACCEPTED" ~/Chimera/logs/chimera.log`
- [ ] Order routed: `grep "ORDER ROUTED" ~/Chimera/logs/chimera.log`
- [ ] Position opened: `grep "🔶 OPEN" ~/Chimera/logs/chimera.log`
- [ ] Position closed: `grep "🏁 CLOSE" ~/Chimera/logs/chimera.log`

## EXPECTED LOG SEQUENCE
```
[OFI] z=1.234
[IMPULSE] 0.567bps
[FadeETH] 🔥 SIGNAL GENERATED (ECONOMICALLY APPROVED)
[DECISION] symbol=ETHUSDT edge=1.234 qty=0.123 regime=STABLE strategy=FADE
[ROUTER] ✅ DECISION ACCEPTED: symbol=ETHUSDT edge=1.234 qty=0.123 mode=SHADOW
[ROUTER] 🎯 ORDER ROUTED: ETHUSDT BUY qty=0.123 edge=1.234bps #1
[SHADOW_EXEC] 🔶 OPEN ETHUSDT BUY qty=0.123 @ 3007.76 TP=3009.84 SL=3006.12
[SHADOW_EXEC] 🏁 CLOSE ETHUSDT BUY PnL=2.45bps (TP)
[TRADES] ETH: 1 trades, 2.45bps cumulative
```

## FAILURE MODES & FIXES

### No [DECISION] logs after 10 minutes
**Cause**: No signals being generated  
**Check**: OFI and impulse values in logs  
**Fix**: Wait for market activity

### [DECISION] but no [ROUTER]
**Cause**: Router crashed or not wired  
**Check**: `sudo systemctl status chimera`  
**Fix**: Check logs for segfault or errors

### [ROUTER] but no [SHADOW_EXEC]
**Cause**: ShadowExecutionEngine issue  
**Check**: Grep for "SHADOW_EXEC" errors  
**Fix**: Verify DecisionEvent format

### Compile errors
**Cause**: Incremental build with old .o files  
**Fix**: `cd build && rm -rf * && cmake .. && make -j2`

## ROLLBACK PROCEDURE
```bash
sudo systemctl stop chimera
cd ~
rm -rf Chimera
mv Chimera_archive_YYYYMMDD_HHMMSS Chimera  # Use actual archive timestamp
cd Chimera/build && rm -rf * && cmake .. && make -j2
sudo systemctl start chimera
```

## SUCCESS CRITERIA
✅ At least 1 [DECISION] log per minute during active market hours  
✅ At least 1 trade opened within 15 minutes  
✅ At least 1 trade closed within 30 minutes  
✅ No crashes or service restarts  
✅ Cumulative PnL tracking correctly  

## MONITORING COMMANDS
```bash
# Watch live logs
tail -f ~/Chimera/logs/chimera.log

# Count decisions
grep -c "[DECISION]" ~/Chimera/logs/chimera.log

# Count trades
grep -c "🔶 OPEN" ~/Chimera/logs/chimera.log

# Check PnL
grep "cumulative" ~/Chimera/logs/chimera.log | tail -5

# Check service uptime
sudo systemctl status chimera | grep "Active"
```

---
**Remember**: This is a LOW-RISK patch. If anything looks wrong, rollback immediately.
