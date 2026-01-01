// =============================================================================
// EdgeRecoveryRules.cpp - v4.8.0 - EDGE RECOVERY RULES IMPLEMENTATION
// =============================================================================
#include "audit/EdgeRecoveryRules.hpp"

#include <chrono>

namespace Chimera {

using Clock = std::chrono::system_clock;

void EdgeRecoveryRules::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.clear();
}

EdgeRecoveryState EdgeRecoveryRules::getState(const std::string& profile) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = state_.find(profile);
    if (it == state_.end()) {
        EdgeRecoveryState empty;
        empty.profile = profile;
        return empty;
    }
    return it->second;
}

void EdgeRecoveryRules::printStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  EDGE RECOVERY STATUS                                         ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    
    if (state_.empty()) {
        printf("║  No recovery tracking active                                  ║\n");
    } else {
        for (const auto& [profile, s] : state_) {
            printf("║  %-15s: %2d healthy / %2d clean | ret=%.2f pay=%.2f    ║\n",
                   profile.c_str(),
                   s.consecutive_healthy_sessions,
                   s.consecutive_clean_days,
                   s.last_edge_retention,
                   s.last_payoff_ratio);
        }
    }
    
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
}

void EdgeRecoveryRules::evaluate(
    const std::string& profile,
    const RollingEdgeReport& rolling,
    const DailyAuditReport& daily,
    ProfileGovernor& governor
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& s = state_[profile];
    s.profile = profile;
    s.last_update = Clock::now();

    s.last_edge_retention = rolling.edge_retention;
    s.last_payoff_ratio = rolling.payoff_ratio;
    s.last_max_drawdown_r = rolling.max_drawdown_r;

    // Track clean daily behavior
    if (daily.verdict == "PASS") {
        s.consecutive_clean_days++;
    } else {
        s.consecutive_clean_days = 0;
    }

    // Track rolling edge health
    if (rolling.verdict == RollingEdgeVerdict::HEALTHY) {
        s.consecutive_healthy_sessions++;
    } else {
        s.consecutive_healthy_sessions = 0;
    }

    ProfileState current = governor.getState(profile);

    // =========================================================================
    // DISABLED → THROTTLED (CONSERVATIVE)
    // =========================================================================
    if (current == ProfileState::DISABLED) {
        if (canRecoverFromDisabled(rolling, daily) &&
            s.consecutive_healthy_sessions >= DISABLED_HEALTHY_SESSIONS_REQUIRED &&
            s.consecutive_clean_days >= DISABLED_CLEAN_DAYS_REQUIRED) {

            governor.setState(profile, ProfileState::THROTTLED);
            printf("[EDGE-RECOVERY] 🔄 %s: DISABLED → THROTTLED (after %d healthy sessions)\n",
                   profile.c_str(), s.consecutive_healthy_sessions);
            
            // Reset counters for next recovery phase
            s.consecutive_healthy_sessions = 0;
            s.consecutive_clean_days = 0;
        }
        return;
    }

    // =========================================================================
    // THROTTLED → ENABLED (STRICT)
    // =========================================================================
    if (current == ProfileState::THROTTLED) {
        if (canRecoverFromThrottled(rolling, daily) &&
            s.consecutive_healthy_sessions >= THROTTLED_HEALTHY_SESSIONS_REQUIRED &&
            s.consecutive_clean_days >= THROTTLED_CLEAN_DAYS_REQUIRED) {

            governor.setState(profile, ProfileState::ENABLED);
            printf("[EDGE-RECOVERY] ✅ %s: THROTTLED → ENABLED (after %d healthy sessions)\n",
                   profile.c_str(), s.consecutive_healthy_sessions);
            
            // Reset counters
            s.consecutive_healthy_sessions = 0;
            s.consecutive_clean_days = 0;
        }
        return;
    }

    // ENABLED: nothing to do here
}

bool EdgeRecoveryRules::canRecoverFromDisabled(
    const RollingEdgeReport& r,
    const DailyAuditReport& d
) const {
    // VERY STRICT — proof of recovery
    if (r.edge_retention < DISABLED_EDGE_RETENTION_MIN) return false;
    if (r.payoff_ratio < DISABLED_PAYOFF_MIN) return false;
    if (r.max_drawdown_r > DISABLED_MAX_DD_MAX) return false;

    if (d.avg_loss_r > DISABLED_AVG_LOSS_MAX) return false;
    if (d.max_trade_loss_r > DISABLED_MAX_LOSS_MAX) return false;

    return true;
}

bool EdgeRecoveryRules::canRecoverFromThrottled(
    const RollingEdgeReport& r,
    const DailyAuditReport& d
) const {
    // Strict but achievable
    if (r.edge_retention < THROTTLED_EDGE_RETENTION_MIN) return false;
    if (r.payoff_ratio < THROTTLED_PAYOFF_MIN) return false;
    if (r.max_drawdown_r > THROTTLED_MAX_DD_MAX) return false;

    if (d.avg_loss_r > THROTTLED_AVG_LOSS_MAX) return false;
    if (d.max_trade_loss_r > THROTTLED_MAX_LOSS_MAX) return false;

    return true;
}

} // namespace Chimera
