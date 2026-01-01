// =============================================================================
// Audit.hpp - v4.8.0 - COMPLETE GOVERNANCE SYSTEM MASTER INCLUDE
// =============================================================================
// PURPOSE: Single include for all audit and governance functionality
//
// GOVERNANCE HIERARCHY (HIGHEST → LOWEST):
//   1. Latency / Shock / Risk exits
//   2. DailyHealthAudit (hard stop)
//   3. RollingEdgeAudit (slow throttle)
//   4. EdgeRecoveryRules (conservative re-enable)
//   5. GoNoGoGate (session start decision)
//   6. Strategy logic
//
// GUARANTEES:
//   ✅ Entry discipline
//   ✅ Exit integrity
//   ✅ Daily behavioral audit
//   ✅ Rolling edge decay detection
//   ✅ Automatic throttling
//   ✅ Automatic disabling
//   ✅ Automatic recovery (conservative)
//   ✅ Session go/no-go decision
//   ✅ Zero discretionary overrides
//
// This is complete professional governance.
//
// USAGE:
//   #include "audit/Audit.hpp"
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

// Core structures
#include "TradeRecord.hpp"
#include "DailyAuditReport.hpp"
#include "RollingEdgeReport.hpp"
#include "EdgeRecoveryState.hpp"
#include "GoNoGoDecision.hpp"

// Audit engines
#include "DailyHealthAudit.hpp"
#include "RollingEdgeAudit.hpp"

// Governance
#include "ProfileGovernor.hpp"
#include "EdgeRecoveryRules.hpp"
#include "GoNoGoGate.hpp"

// Utilities
#include "DailyReportExporter.hpp"
#include "LiveHealthSnapshot.hpp"

namespace Chimera {

// =============================================================================
// COMPLETE END-OF-SESSION WORKFLOW
// =============================================================================
// Call this at the end of each trading session
inline void runCompleteSessionAudit(const std::string& profile = "SCALP_FAST") {
    // 1. Run daily audit
    DailyAuditReport dailyReport = getDailyAudit().runDailyAudit();
    dailyReport.print();
    
    // 2. Get rolling edge report
    RollingEdgeReport rollingReport = getRollingEdgeAudit().evaluateProfile(profile);
    rollingReport.print();
    
    // 3. Apply daily audit enforcement
    getProfileGovernor().applyAuditVerdict(profile, dailyReport.verdict);
    
    // 4. Apply rolling edge enforcement
    if (rollingReport.verdict == RollingEdgeVerdict::BROKEN) {
        getProfileGovernor().setState(profile, ProfileState::DISABLED);
        printf("[AUDIT] 🔴 %s DISABLED due to BROKEN rolling edge\n", profile.c_str());
    } else if (rollingReport.verdict == RollingEdgeVerdict::DEGRADING) {
        // Only throttle if not already disabled
        if (getProfileGovernor().getState(profile) != ProfileState::DISABLED) {
            getProfileGovernor().setState(profile, ProfileState::THROTTLED);
            printf("[AUDIT] 🟡 %s THROTTLED due to DEGRADING rolling edge\n", profile.c_str());
        }
    }
    
    // 5. Evaluate edge recovery
    getEdgeRecoveryRules().evaluate(profile, rollingReport, dailyReport, getProfileGovernor());
    
    // 6. Export daily report
    DailyReportExporter::exportToday(dailyReport);
    
    // 7. Print final status
    getProfileGovernor().printStatus();
    getEdgeRecoveryRules().printStatus();
    
    printf("[AUDIT] End-of-session audit complete. Daily: %s | Rolling: %s\n",
           dailyReport.verdict.c_str(), rollingEdgeVerdictToString(rollingReport.verdict));
}

// =============================================================================
// SESSION START GO/NO-GO CHECK
// =============================================================================
// Call this before each trading session starts
inline GoNoGoDecision checkSessionReadiness(
    const std::string& session,
    const std::string& profile,
    bool latencyStable,
    bool shockActive
) {
    // Get last daily report (would need to persist between sessions in production)
    DailyAuditReport dailyReport = getDailyAudit().runDailyAudit();
    
    // Get rolling edge report
    RollingEdgeReport rollingReport = getRollingEdgeAudit().evaluateProfile(profile);
    
    // Evaluate go/no-go
    GoNoGoDecision decision = getGoNoGoGate().evaluateSimple(
        session, profile, dailyReport, rollingReport,
        getProfileGovernor(), latencyStable, shockActive
    );
    
    decision.print();
    return decision;
}

// =============================================================================
// MULTI-PROFILE SESSION START
// =============================================================================
inline GoNoGoDecision checkMultiProfileReadiness(
    const std::string& session,
    const std::vector<std::string>& profiles,
    bool latencyStable,
    bool shockActive
) {
    DailyAuditReport dailyReport = getDailyAudit().runDailyAudit();
    
    std::unordered_map<std::string, RollingEdgeReport> rollingReports;
    for (const auto& profile : profiles) {
        rollingReports[profile] = getRollingEdgeAudit().evaluateProfile(profile);
    }
    
    GoNoGoDecision decision = getGoNoGoGate().evaluate(
        session, dailyReport, rollingReports,
        getProfileGovernor(), latencyStable, shockActive
    );
    
    decision.print();
    return decision;
}

// =============================================================================
// START-OF-DAY RESET
// =============================================================================
// Call this at the start of each trading day
inline void startNewTradingDay() {
    getDailyAudit().reset();
    printf("[AUDIT] New trading day started. Daily audit state reset.\n");
    printf("[AUDIT] Rolling edge audit and profile governor states preserved.\n");
    getProfileGovernor().printStatus();
}

// =============================================================================
// FULL SYSTEM RESET (USE WITH CAUTION)
// =============================================================================
inline void resetAllAuditState() {
    getDailyAudit().reset();
    getRollingEdgeAudit().reset();
    getEdgeRecoveryRules().reset();
    getProfileGovernor().resetAll();
    printf("[AUDIT] ⚠️ FULL SYSTEM RESET - All audit state cleared\n");
}

} // namespace Chimera
