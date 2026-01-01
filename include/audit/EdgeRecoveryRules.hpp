// =============================================================================
// EdgeRecoveryRules.hpp - v4.8.0 - EDGE RECOVERY RULES ENGINE
// =============================================================================
// PURPOSE: Automatic, conservative re-enablement of profiles after edge recovery
//
// GUARANTEES:
//   ❌ No mid-session re-enable
//   ❌ No manual override
//   ❌ No direct DISABLED → ENABLED
//   ❌ No recovery after bad daily behavior
//   ✅ Requires sustained edge proof
//
// RECOVERY PATH:
//   DISABLED → THROTTLED → ENABLED
//
// USAGE:
//   EdgeRecoveryRules& recovery = getEdgeRecoveryRules();
//   recovery.evaluate("SCALP_FAST", rollingReport, dailyReport, governor);
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include "RollingEdgeReport.hpp"
#include "DailyAuditReport.hpp"
#include "ProfileGovernor.hpp"
#include "EdgeRecoveryState.hpp"

#include <unordered_map>
#include <string>
#include <mutex>

namespace Chimera {

class EdgeRecoveryRules {
public:
    EdgeRecoveryRules() = default;

    // =========================================================================
    // EVALUATE RECOVERY (CALL BETWEEN SESSIONS)
    // =========================================================================
    void evaluate(
        const std::string& profile,
        const RollingEdgeReport& rolling,
        const DailyAuditReport& daily,
        ProfileGovernor& governor
    );

    // =========================================================================
    // RESET
    // =========================================================================
    void reset();
    
    // =========================================================================
    // GET STATE
    // =========================================================================
    EdgeRecoveryState getState(const std::string& profile) const;
    
    // =========================================================================
    // PRINT STATUS
    // =========================================================================
    void printStatus() const;
    
    // =========================================================================
    // SINGLETON
    // =========================================================================
    static EdgeRecoveryRules& instance() {
        static EdgeRecoveryRules inst;
        return inst;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, EdgeRecoveryState> state_;

    // =========================================================================
    // RECOVERY THRESHOLDS (VERY STRICT)
    // =========================================================================
    
    // DISABLED → THROTTLED requirements
    static constexpr int DISABLED_HEALTHY_SESSIONS_REQUIRED = 5;
    static constexpr int DISABLED_CLEAN_DAYS_REQUIRED = 3;
    static constexpr double DISABLED_EDGE_RETENTION_MIN = 0.65;
    static constexpr double DISABLED_PAYOFF_MIN = 1.6;
    static constexpr double DISABLED_MAX_DD_MAX = 2.0;
    static constexpr double DISABLED_AVG_LOSS_MAX = 0.9;
    static constexpr double DISABLED_MAX_LOSS_MAX = 1.1;
    
    // THROTTLED → ENABLED requirements (stricter)
    static constexpr int THROTTLED_HEALTHY_SESSIONS_REQUIRED = 10;
    static constexpr int THROTTLED_CLEAN_DAYS_REQUIRED = 5;
    static constexpr double THROTTLED_EDGE_RETENTION_MIN = 0.70;
    static constexpr double THROTTLED_PAYOFF_MIN = 1.7;
    static constexpr double THROTTLED_MAX_DD_MAX = 1.5;
    static constexpr double THROTTLED_AVG_LOSS_MAX = 0.8;
    static constexpr double THROTTLED_MAX_LOSS_MAX = 1.0;

    bool canRecoverFromDisabled(
        const RollingEdgeReport& rolling,
        const DailyAuditReport& daily
    ) const;

    bool canRecoverFromThrottled(
        const RollingEdgeReport& rolling,
        const DailyAuditReport& daily
    ) const;
};

// =========================================================================
// CONVENIENCE FUNCTION
// =========================================================================
inline EdgeRecoveryRules& getEdgeRecoveryRules() {
    return EdgeRecoveryRules::instance();
}

} // namespace Chimera
