// =============================================================================
// GoNoGoGate.cpp - v4.8.0 - GO/NO-GO GATE IMPLEMENTATION
// =============================================================================
#include "audit/GoNoGoGate.hpp"

namespace Chimera {

GoNoGoDecision GoNoGoGate::evaluate(
    const std::string& session,
    const DailyAuditReport& daily,
    const std::unordered_map<std::string, RollingEdgeReport>& rolling,
    const ProfileGovernor& governor,
    bool latencyStable,
    bool shockActive
) const {
    GoNoGoDecision d;
    d.session = session;

    // =========================================================================
    // HARD GLOBAL BLOCKERS (HIGHEST PRIORITY)
    // =========================================================================
    
    if (!latencyStable) {
        d.status = GoNoGoStatus::NO_GO;
        d.reason = "LATENCY_UNSTABLE";
        return d;
    }

    if (shockActive) {
        d.status = GoNoGoStatus::NO_GO;
        d.reason = "NEWS_SHOCK_ACTIVE";
        return d;
    }

    // =========================================================================
    // DAILY HEALTH CHECK
    // =========================================================================
    
    if (daily.fail) {
        d.status = GoNoGoStatus::NO_GO;
        d.reason = "DAILY_AUDIT_FAIL";
        return d;
    }

    // =========================================================================
    // PROFILE HEALTH CHECK
    // =========================================================================
    
    bool anyEnabled = false;

    for (const auto& [profile, rep] : rolling) {
        ProfileState state = governor.getState(profile);

        // Profile is disabled by governor
        if (state == ProfileState::DISABLED) {
            d.blocking_profiles.push_back(profile);
            continue;
        }

        // Profile edge is broken
        if (rep.verdict == RollingEdgeVerdict::BROKEN) {
            d.blocking_profiles.push_back(profile);
            continue;
        }

        // Profile is enabled or throttled with healthy edge
        if (state == ProfileState::ENABLED || state == ProfileState::THROTTLED) {
            anyEnabled = true;
            d.enabled_profiles.push_back(profile);
        }
    }

    if (!anyEnabled) {
        d.status = GoNoGoStatus::NO_GO;
        d.reason = "NO_HEALTHY_PROFILES";
        return d;
    }

    // =========================================================================
    // FINAL DECISION
    // =========================================================================
    
    d.status = GoNoGoStatus::GO;
    d.reason = "ALL_CHECKS_PASSED";
    return d;
}

GoNoGoDecision GoNoGoGate::evaluateSimple(
    const std::string& session,
    const std::string& profile,
    const DailyAuditReport& daily,
    const RollingEdgeReport& rolling,
    const ProfileGovernor& governor,
    bool latencyStable,
    bool shockActive
) const {
    std::unordered_map<std::string, RollingEdgeReport> rollingMap;
    rollingMap[profile] = rolling;
    return evaluate(session, daily, rollingMap, governor, latencyStable, shockActive);
}

} // namespace Chimera
