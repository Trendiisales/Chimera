// =============================================================================
// EdgeRecoveryState.hpp - v4.8.0 - EDGE RECOVERY STATE TRACKING
// =============================================================================
// PURPOSE: Track recovery progress for throttled/disabled profiles
//
// RECOVERY RULES:
//   - DISABLED → THROTTLED: 5 consecutive healthy sessions + 3 clean days
//   - THROTTLED → ENABLED: 10 consecutive healthy sessions + 5 clean days
//   - NEVER: DISABLED → ENABLED directly
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include <string>
#include <chrono>
#include <cstdio>

namespace Chimera {

struct EdgeRecoveryState {
    std::string profile;

    int consecutive_healthy_sessions = 0;
    int consecutive_clean_days = 0;

    double last_edge_retention = 0.0;
    double last_payoff_ratio = 0.0;
    double last_max_drawdown_r = 0.0;

    std::chrono::system_clock::time_point last_update;
    
    void reset() {
        consecutive_healthy_sessions = 0;
        consecutive_clean_days = 0;
        last_edge_retention = 0.0;
        last_payoff_ratio = 0.0;
        last_max_drawdown_r = 0.0;
    }
    
    void print() const {
        printf("[RECOVERY-STATE] %s: %d healthy sessions, %d clean days | "
               "retention=%.2f payoff=%.2f dd=%.2fR\n",
               profile.c_str(),
               consecutive_healthy_sessions,
               consecutive_clean_days,
               last_edge_retention,
               last_payoff_ratio,
               last_max_drawdown_r);
    }
};

} // namespace Chimera
