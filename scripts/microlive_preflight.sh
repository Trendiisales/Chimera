#!/bin/bash
# =============================================================================
# microlive_preflight.sh - Chimera v4.10.2 Micro-Live Deployment Checklist
# =============================================================================
# Run this BEFORE starting micro-live trading:
#   chmod +x microlive_preflight.sh
#   ./microlive_preflight.sh
# =============================================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

PASS=0
FAIL=0
WARN=0

header() {
    echo ""
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${CYAN}  $1${NC}"
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
}

check_pass() {
    echo -e "  [${GREEN}✓${NC}] $1"
    ((PASS++))
}

check_fail() {
    echo -e "  [${RED}✗${NC}] $1"
    ((FAIL++))
}

check_warn() {
    echo -e "  [${YELLOW}!${NC}] $1"
    ((WARN++))
}

# =============================================================================
header "CHIMERA v4.10.2 MICRO-LIVE PREFLIGHT CHECK"
# =============================================================================

echo ""
echo "Date: $(date)"
echo "User: $(whoami)"
echo "Host: $(hostname)"
echo ""

# =============================================================================
header "1. CONFIGURATION VERIFICATION"
# =============================================================================

# Check if we're in the Chimera directory
if [ -f "CMakeLists.txt" ] && grep -q "chimera" CMakeLists.txt 2>/dev/null; then
    check_pass "In Chimera directory"
else
    check_fail "Not in Chimera directory - cd to ~/Chimera first"
fi

# Check v4.10.2 files exist
if [ -f "include/engines/IndexE2Engine.hpp" ]; then
    check_pass "IndexE2Engine.hpp exists"
else
    check_fail "IndexE2Engine.hpp missing - deploy v4.10.2 files"
fi

if [ -f "include/audit/MicroLiveAuditLogger.hpp" ]; then
    check_pass "MicroLiveAuditLogger.hpp exists"
else
    check_fail "MicroLiveAuditLogger.hpp missing"
fi

if [ -f "chimera_dashboard_microlive.html" ]; then
    check_pass "Micro-live dashboard exists"
else
    check_warn "Micro-live dashboard missing (optional)"
fi

# Check config.ini
if [ -f "config.ini" ]; then
    check_pass "config.ini exists"
    
    # Check cTrader settings
    if grep -q "enable = true" config.ini && grep -q "ctrader" config.ini; then
        check_pass "cTrader enabled in config"
    else
        check_warn "Verify cTrader settings in config.ini"
    fi
else
    check_fail "config.ini missing"
fi

# =============================================================================
header "2. BUILD VERIFICATION"
# =============================================================================

# Check if build directory exists
if [ -d "build" ]; then
    check_pass "Build directory exists"
else
    check_warn "Build directory missing - run cmake first"
fi

# Check if executable exists
if [ -f "build/chimera" ] || [ -f "build/chimera.exe" ]; then
    check_pass "Chimera executable exists"
    
    # Check executable is recent
    EXE_AGE=$(find build -name "chimera*" -type f -mmin -60 2>/dev/null | wc -l)
    if [ "$EXE_AGE" -gt 0 ]; then
        check_pass "Executable built within last hour"
    else
        check_warn "Executable may be stale - consider rebuilding"
    fi
else
    check_fail "Chimera executable not found - build required"
fi

# =============================================================================
header "3. SYMBOL SCOPE VERIFICATION"
# =============================================================================

echo -e "  ${YELLOW}LOCKED SYMBOLS:${NC}"
echo "    ✓ NAS100 (0.5% risk)"
echo "    ✓ US30 (0.4% risk)"
echo ""
echo -e "  ${RED}DISABLED (MUST NOT TRADE):${NC}"
echo "    ✗ Crypto (BTC/ETH/SOL)"
echo "    ✗ Gold (XAUUSD - isolated)"
echo "    ✗ Forex (EURUSD etc)"
echo ""

# Verify in main_triple.cpp or config
if grep -q "MICRO_LIVE" src/main*.cpp 2>/dev/null || grep -q "IndexE2Engine" src/main*.cpp 2>/dev/null; then
    check_pass "Micro-live mode detected in source"
else
    check_warn "Verify main entry point uses E2 engines"
fi

# =============================================================================
header "4. RISK PARAMETERS (LOCKED)"
# =============================================================================

echo "  The following MUST NOT be changed during micro-live:"
echo ""
echo "  ┌────────────────────────────────────────┐"
echo "  │ Symbol   │ Risk    │ Max Trades/Day   │"
echo "  ├────────────────────────────────────────┤"
echo "  │ NAS100   │ 0.50%   │ 1                │"
echo "  │ US30     │ 0.40%   │ 1                │"
echo "  └────────────────────────────────────────┘"
echo ""
echo "  Time Window: 10:05 - 14:30 NY"
echo "  Daily Halt: -2.0R"
echo "  Exit Rules: Partial 60% at +1R, Stall kill 6 bars"
echo ""

check_warn "Manually verify risk parameters match above"

# =============================================================================
header "5. NETWORK CONNECTIVITY"
# =============================================================================

# Check cTrader FIX connectivity
if ping -c 1 demo-uk-eqx-01.p.c-trader.com &>/dev/null; then
    check_pass "cTrader server reachable"
else
    check_fail "Cannot reach cTrader server"
fi

# Check if GUI port is available
if ! netstat -tuln 2>/dev/null | grep -q ":7777 " && ! ss -tuln 2>/dev/null | grep -q ":7777 "; then
    check_pass "GUI port 7777 available"
else
    check_warn "Port 7777 may be in use"
fi

# =============================================================================
header "6. LOG DIRECTORIES"
# =============================================================================

# Create log directories
mkdir -p logs/microlive 2>/dev/null
if [ -d "logs/microlive" ]; then
    check_pass "Micro-live log directory exists"
else
    check_fail "Could not create log directory"
fi

# Check disk space
AVAIL=$(df -BG . 2>/dev/null | tail -1 | awk '{print $4}' | tr -d 'G')
if [ ! -z "$AVAIL" ] && [ "$AVAIL" -gt 1 ]; then
    check_pass "Sufficient disk space (${AVAIL}GB available)"
else
    check_warn "Check disk space availability"
fi

# =============================================================================
header "7. PROCESS CHECK"
# =============================================================================

# Check if Chimera is already running
if pgrep -x "chimera" &>/dev/null; then
    check_fail "Chimera already running - stop existing instance first"
    echo "    Run: pkill chimera"
else
    check_pass "No existing Chimera process"
fi

# Check lock file
if [ -f "/tmp/chimera.lock" ]; then
    check_warn "Lock file exists - may need cleanup"
    echo "    Run: rm /tmp/chimera.lock"
else
    check_pass "No stale lock file"
fi

# =============================================================================
header "8. BROKER ACCOUNT VERIFICATION"
# =============================================================================

echo "  ⚠️  MANUAL CHECKS REQUIRED:"
echo ""
echo "  □ Logged into cTrader account"
echo "  □ Account has sufficient margin"
echo "  □ No pending orders from previous session"
echo "  □ Account is live (not demo) - if intended"
echo ""

check_warn "Manually verify broker account status"

# =============================================================================
header "PREFLIGHT SUMMARY"
# =============================================================================

echo ""
echo "  Passed: ${PASS}"
echo "  Failed: ${FAIL}"
echo "  Warnings: ${WARN}"
echo ""

if [ "$FAIL" -eq 0 ]; then
    echo -e "  ${GREEN}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "  ${GREEN}  ✓ PREFLIGHT COMPLETE - READY FOR MICRO-LIVE${NC}"
    echo -e "  ${GREEN}═══════════════════════════════════════════════════════════════${NC}"
    echo ""
    echo "  To start micro-live:"
    echo "    cd build && ./chimera"
    echo ""
    echo "  To view dashboard:"
    echo "    Open: http://localhost:8081/chimera_dashboard_microlive.html"
    echo "    (Run: python3 -m http.server 8081 in Chimera dir)"
    echo ""
else
    echo -e "  ${RED}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "  ${RED}  ✗ PREFLIGHT FAILED - FIX ISSUES BEFORE PROCEEDING${NC}"
    echo -e "  ${RED}═══════════════════════════════════════════════════════════════${NC}"
    exit 1
fi

# =============================================================================
header "MICRO-LIVE SUCCESS CRITERIA (FIRST 20-30 TRADES)"
# =============================================================================

echo "  You are not judging P&L yet. You are checking:"
echo ""
echo "  □ Trade frequency ≈ 1-2/day"
echo "  □ Exit distribution resembles backtest"
echo "  □ No runaway losers"
echo "  □ No machine-gun entries"
echo "  □ Emotional response is bored, not stressed"
echo ""
echo "  If these hold → system is production-grade."
echo ""

# =============================================================================
header "WHAT YOU ARE NOT ALLOWED TO CHANGE"
# =============================================================================

echo "  Absolutely do not:"
echo ""
echo "  ✗ Adjust filters"
echo "  ✗ Adjust time windows"
echo "  ✗ Adjust risk"
echo "  ✗ Add SPX500"
echo "  ✗ Add FX"
echo "  ✗ \"Fix\" a losing streak"
echo ""
echo "  If something feels wrong → stop, inspect logs, don't tweak."
echo ""

exit 0
