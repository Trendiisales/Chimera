# CHIMERA v4.12.0 DEPLOYMENT GUIDE
## DECISION VISIBILITY PATCH

## WHAT THIS PATCH DOES

1. **Adds explicit [DECISION] logging** before every router call
2. **Enhances router logging** to show acceptance/rejection clearly
3. **Creates TradePolicy.hpp** - strategy-aware regime framework (infrastructure only)
4. **Clarifies shadow mode behavior** with better comments and logs

**NO BEHAVIORAL CHANGES** - This is a pure logging/visibility patch.

## DEPLOYMENT STEPS (Osaka VPS)

### 1. Download & Prepare
```bash
# From your Mac
scp ~/Downloads/Chimera_v4_12_0_DECISION_VISIBILITY_PATCH.tar.gz chimera:~/

# SSH to VPS
ssh chimera

# Archive existing deployment (MANDATORY)
mv ~/Chimera ~/Chimera_archive_$(date +%Y%m%d_%H%M%S)
```

### 2. Extract & Build
```bash
# Extract new version
cd ~
tar xzf Chimera_v4_12_0_DECISION_VISIBILITY_PATCH.tar.gz
mv Chimera_v4_8_0_FINAL Chimera

# Clean build (MANDATORY)
cd ~/Chimera/build
rm -rf *
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j2
```

### 3. Verify Build
```bash
# Look for this at the end:
# [100%] Built target chimera

# If you see errors, STOP and report back
```

### 4. Deploy
```bash
# Stop service
sudo systemctl stop chimera

# Start service
sudo systemctl start chimera

# Check status
sudo systemctl status chimera

# Watch logs
tail -f ~/Chimera/logs/chimera.log
```

## VERIFICATION

Within 30 seconds of startup, you should see:

```
[DECISION] symbol=ETHUSDT edge=X.XX qty=X.XX regime=STABLE strategy=FADE
[ROUTER] ✅ DECISION ACCEPTED: symbol=ETHUSDT edge=X.XX qty=X.XX mode=SHADOW
[ROUTER] 🎯 ORDER ROUTED: ETHUSDT BUY qty=0.123 edge=1.234bps #1
[SHADOW_EXEC] 🔶 OPEN ETHUSDT BUY qty=0.123 @ 3007.76 TP=3009.84 SL=3006.12
```

If you see [DECISION] logs, the patch is working correctly.

## EXPECTED BEHAVIOR

- **No change** in trade frequency
- **No change** in PnL patterns
- **More visibility** into decision pipeline
- **Clearer logs** for debugging

## QUICK TESTS

### Test 1: Verify Decision Logging
```bash
cd ~/Chimera/logs
grep "[DECISION]" chimera.log | head -5
```
Expected: 5 decision lines with symbol, edge, qty, regime, strategy

### Test 2: Verify Router Acceptance
```bash
grep "DECISION ACCEPTED" chimera.log | head -5
```
Expected: 5 acceptance lines showing mode=SHADOW

### Test 3: Verify Shadow Execution
```bash
grep "SHADOW_EXEC" chimera.log | grep "OPEN\|CLOSE" | head -10
```
Expected: Alternating OPEN and CLOSE messages

## ROLLBACK (If Needed)

If something goes wrong:
```bash
sudo systemctl stop chimera
cd ~
rm -rf Chimera
mv Chimera_archive_YYYYMMDD_HHMMSS Chimera  # Use your actual archive name
cd ~/Chimera/build
rm -rf *
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j2
sudo systemctl start chimera
```

## FILES CHANGED

1. `include/execution/TradePolicy.hpp` - NEW (strategy framework)
2. `src/FadeETH.cpp` - MODIFIED (added [DECISION] log)
3. `src/FadeSOL.cpp` - MODIFIED (added [DECISION] log)
4. `src/execution/DecisionRouter.cpp` - MODIFIED (enhanced logging)
5. `PATCH_NOTES_v4.12.0.md` - NEW (this documentation)

## SETTINGS

**NO CHANGES REQUIRED**

All existing settings remain valid:
- ETH: $800, 1.5bps floor, 1.2bps impulse, 3.0bps TP, 1.6bps SL, 1500ms cooldown
- SOL: $400, 2.5bps floor, 2.0bps impulse, 5.0bps TP, 2.5bps SL, 2500ms cooldown

## TROUBLESHOOTING

### Issue: No [DECISION] logs
**Cause**: Not generating signals (check OFI/impulse)  
**Fix**: Wait for market activity, check depth feeds

### Issue: [DECISION] but no [ROUTER]
**Cause**: Router not wired or crashed  
**Fix**: Check systemctl status, review logs for errors

### Issue: [ROUTER] but no [SHADOW_EXEC]
**Cause**: ShadowExecutionEngine not receiving decisions  
**Fix**: Check DecisionEvent creation in strategies

### Issue: Compile errors
**Cause**: Clean build not performed  
**Fix**: `cd build && rm -rf * && cmake .. && make -j2`

## SUPPORT

If you encounter issues:
1. Capture full error output
2. Share last 100 lines of chimera.log
3. Share systemctl status output
4. Include build output if compile failed

---
**Version**: 4.12.0  
**Type**: Logging/Visibility Patch  
**Risk Level**: LOW (no behavioral changes)  
**Deployment Time**: ~5 minutes  
**Rollback Time**: ~3 minutes  
**Status**: READY FOR PRODUCTION
