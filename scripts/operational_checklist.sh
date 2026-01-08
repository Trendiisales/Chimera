#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════════════
# CHIMERA v4.9.11 OPERATIONAL CHECKLIST
# ═══════════════════════════════════════════════════════════════════════════════
# Run this checklist once before production deployment
# Each step verifies a critical system component
# ═══════════════════════════════════════════════════════════════════════════════

echo "═══════════════════════════════════════════════════════════════════════════════"
echo "CHIMERA v4.9.11 - OPERATIONAL CHECKLIST"
echo "═══════════════════════════════════════════════════════════════════════════════"
echo ""

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

pass() {
    echo -e "[${GREEN}✓${NC}] $1"
}

fail() {
    echo -e "[${RED}✗${NC}] $1"
}

warn() {
    echo -e "[${YELLOW}!${NC}] $1"
}

info() {
    echo -e "[i] $1"
}

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "PRE-FLIGHT CHECKS"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# 1. Check binary exists
if [ -f "./chimera" ]; then
    pass "Binary exists: ./chimera"
else
    fail "Binary not found: ./chimera"
    echo "    Run: cmake .. && make -j4"
fi

# 2. Check config exists
if [ -f "./config.ini" ]; then
    pass "Config exists: ./config.ini"
else
    fail "Config not found: ./config.ini"
fi

# 3. Check runtime directories
mkdir -p runtime/audit runtime/profiles runtime/logs 2>/dev/null
if [ -d "runtime/audit" ] && [ -d "runtime/profiles" ]; then
    pass "Runtime directories created"
else
    fail "Could not create runtime directories"
fi

# 4. Check dashboard
if [ -f "./chimera_dashboard.html" ]; then
    pass "Dashboard exists: ./chimera_dashboard.html"
else
    warn "Dashboard not found (optional)"
fi

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "COMPONENT VERIFICATION"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Check header files exist
HEADERS=(
    "include/bootstrap/LatencyBootstrapper.hpp"
    "include/runtime/SystemMode.hpp"
    "include/latency/LatencyGate.hpp"
    "include/execution/ExecutionModeSelector.hpp"
    "include/execution/SessionWeights.hpp"
    "include/execution/SizeScaler.hpp"
    "include/execution/ThresholdAdapter.hpp"
    "include/execution/ExecutionQualityFeedback.hpp"
    "include/execution/SignalHandler.hpp"
    "include/audit/ConvictionAudit.hpp"
    "include/audit/BrokerValidator.hpp"
)

for h in "${HEADERS[@]}"; do
    if [ -f "$h" ]; then
        pass "Header: $h"
    else
        fail "Missing: $h"
    fi
done

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "POST-START VERIFICATION (run after ./chimera starts)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

cat << 'EOF'
[ ] 1. BOOTSTRAP MODE ACTIVE
    - Dashboard shows "PROBE X/Y" during startup
    - System mode = BOOTSTRAP
    - Trades blocked until bootstrap complete

[ ] 2. LATENCY STATS COLLECTING
    - HOT LAT panel shows p50/p99 values
    - Sample count increasing
    - Values are realistic (0.2-5ms for crypto)

[ ] 3. EXECUTION STATS INCREMENTING
    - Orders sent counter > 0
    - ACKs received counter > 0
    - Reject rate < 15%

[ ] 4. TRANSITION TO LIVE
    - After ~30 probes: System mode = LIVE
    - Dashboard shows "LIVE" badge (green)
    - Trades now allowed

[ ] 5. CONVICTION HEATMAP EXPORTING
    After 1 hour:
    - Check: runtime/audit/conviction_heatmap.csv
    - Should have samples with scores, traded flags
    - Distribution should show score 5+ → traded

[ ] 6. BROKER LOGS AVAILABLE
    After first session:
    - Check: runtime/audit/broker_*.log
    - Contains latency percentiles
    - Contains score and grade

[ ] 7. CRYPTO TAKER-ONLY ENFORCED
    - BTC/ETH/SOL execution mode = TAKER
    - No maker orders on crypto
    - Check dashboard execution mode indicator

[ ] 8. KILL-SWITCH ACTIVE
    - Daily loss limit set
    - Drawdown limit set
    - Test: Ctrl+C → clean shutdown

[ ] 9. SESSION WEIGHTS APPLIED
    - Check current session (ASIA/LONDON/NY/OVERLAP)
    - Edge weight applied
    - Size multiplier applied

[10] THRESHOLD ADAPTATION WORKING
    - After 15 min no trades: threshold relaxes
    - On drawdown: threshold tightens
    - Check logs for "STARVATION_RELAX" or "DRAWDOWN_TIGHTEN"

EOF

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "AUDIT EVIDENCE COLLECTION"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

cat << 'EOF'
After a full trading session:

1. Export conviction heatmap:
   - File: runtime/audit/conviction_heatmap.csv
   - Analyze: score distribution, trade rate by hour

2. Export broker comparison:
   - File: runtime/audit/broker_comparison.csv
   - Compare: latency, reject rate, costs

3. Export execution stats:
   - File: runtime/audit/execution_stats.log
   - Check: fill rate, reject rate

4. Archive for compliance:
   tar -czvf audit_$(date +%Y%m%d).tar.gz runtime/audit/

EOF

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "COMPLIANCE ATTESTATION"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

cat << 'EOF'
Chimera v4.9.11 is certified compliant for:

✔ No hard RANGING veto (MEAN_REVERSION intent allowed)
✔ Execution feasibility before conviction scoring
✔ Bootstrap probe orders for latency truth
✔ Latency as cost, not gate
✔ Crypto taker-only (no maker fantasy)
✔ Persistent learning across restarts
✔ Crash-safe shutdown with order cancellation
✔ Audit-grade evidence collection

NOT COMPLIANT FOR:
✗ Queue-position HFT (requires colo)
✗ Sub-millisecond arb (requires colo)
✗ Alpha generation (execution only, not alpha source)

EOF

echo ""
echo "═══════════════════════════════════════════════════════════════════════════════"
echo "Checklist complete. Review any [✗] items before production."
echo "═══════════════════════════════════════════════════════════════════════════════"
