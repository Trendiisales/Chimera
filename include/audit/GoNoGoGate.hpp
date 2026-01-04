// =============================================================================
// GoNoGoGate.hpp - v4.8.0 - SESSION GO/NO-GO GATE
// =============================================================================
// PURPOSE: Evaluate session readiness before any trading begins
//
// EVALUATES:
//   - Yesterday's DailyHealthAudit
//   - RollingEdgeAudit per profile
//   - Current ProfileGovernor states
//   - Recent latency stability
//   - Shock / news blackout windows
//
// GUARANTEES:
//   ❌ You cannot "trade anyway"
//   ❌ You cannot override with confidence
//   ❌ You cannot revenge trade
//   ❌ You cannot ignore decay
//   ✅ System trades only when healthy
//
// USAGE:
//   GoNoGoGate& gate = getGoNoGoGate();
//   GoNoGoDecision decision = gate.evaluate("NY", daily, rolling, governor, latencyOK, shockActive);
//   if (decision.status == GoNoGoStatus::NO_GO) {
//       disableAllTrading(decision.reason);
//   }
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include "GoNoGoDecision.hpp"
#include "DailyAuditReport.hpp"
#include "RollingEdgeReport.hpp"
#include "ProfileGovernor.hpp"

#include <string>
#include <unordered_map>

namespace Chimera {

class GoNoGoGate {
public:
    GoNoGoGate() = default;

    // =========================================================================
    // EVALUATE SESSION READINESS
    // =========================================================================
    GoNoGoDecision evaluate(
        const std::string& session,
        const DailyAuditReport& daily,
        const std::unordered_map<std::string, RollingEdgeReport>& rolling,
        const ProfileGovernor& governor,
        bool latencyStable,
        bool shockActive
    ) const;
    
    // =========================================================================
    // SIMPLIFIED EVALUATE (for single profile)
    // =========================================================================
    GoNoGoDecision evaluateSimple(
        const std::string& session,
        const std::string& profile,
        const DailyAuditReport& daily,
        const RollingEdgeReport& rolling,
        const ProfileGovernor& governor,
        bool latencyStable,
        bool shockActive
    ) const;
    
    // =========================================================================
    // SINGLETON
    // =========================================================================
    static GoNoGoGate& instance() {
        static GoNoGoGate inst;
        return inst;
    }
};

// =========================================================================
// CONVENIENCE FUNCTION
// =========================================================================
inline GoNoGoGate& getGoNoGoGate() {
    return GoNoGoGate::instance();
}

} // namespace Chimera
