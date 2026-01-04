// ═══════════════════════════════════════════════════════════════════════════════
// include/risk/FailureGovernor.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.11: FAILURE CONTAINMENT LAYER
//
// PURPOSE: Prevent account bans, broker throttling, and infrastructure death.
// Required for 1K+ trades/day operation.
//
// MONITORS:
// - Reject rate (per 60s window)
// - Cancel rate (per 60s window)
// - Disconnect count
// - Error rate
//
// ACTIONS:
// - Block trading when thresholds exceeded
// - Cooldown before resuming
// - Gradual recovery
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <atomic>
#include <chrono>

namespace Chimera {
namespace Risk {

// ─────────────────────────────────────────────────────────────────────────────
// Failure State
// ─────────────────────────────────────────────────────────────────────────────
struct FailureState {
    int rejects_60s = 0;
    int cancels_60s = 0;
    int disconnects = 0;
    int errors_60s = 0;
    uint64_t last_reject_ns = 0;
    uint64_t last_error_ns = 0;
    uint64_t cooldown_until_ns = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Failure Thresholds
// ─────────────────────────────────────────────────────────────────────────────
struct FailureThresholds {
    int max_rejects_60s = 20;        // Max rejects per minute
    int max_cancels_60s = 50;        // Max cancels per minute
    int max_disconnects = 3;         // Max disconnects before halt
    int max_errors_60s = 10;         // Max errors per minute
    
    uint64_t cooldown_ns = 60'000'000'000ULL;  // 60 second cooldown
    uint64_t window_ns = 60'000'000'000ULL;    // 60 second window
};

// ─────────────────────────────────────────────────────────────────────────────
// Backoff Tier (Progressive)
// ─────────────────────────────────────────────────────────────────────────────
enum class BackoffTier : uint8_t {
    NONE      = 0,  // No backoff
    LIGHT     = 1,  // 5 second pause
    MODERATE  = 2,  // 30 second pause
    HEAVY     = 3,  // 60 second pause
    SEVERE    = 4,  // 5 minute pause
    HALT      = 5   // Full halt until manual reset
};

struct BackoffState {
    BackoffTier tier = BackoffTier::NONE;
    uint64_t backoff_until_ns = 0;
    int consecutive_triggers = 0;
    
    uint64_t backoffDuration() const {
        switch (tier) {
            case BackoffTier::LIGHT:    return 5'000'000'000ULL;
            case BackoffTier::MODERATE: return 30'000'000'000ULL;
            case BackoffTier::HEAVY:    return 60'000'000'000ULL;
            case BackoffTier::SEVERE:   return 300'000'000'000ULL;
            case BackoffTier::HALT:     return UINT64_MAX;
            default: return 0;
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Check if Trading Allowed
// ─────────────────────────────────────────────────────────────────────────────
inline bool tradingAllowed(
    const FailureState& f,
    uint64_t now_ns,
    const FailureThresholds& t = FailureThresholds{}
) {
    // In cooldown?
    if (now_ns < f.cooldown_until_ns) {
        return false;
    }
    
    // Check thresholds
    if (f.rejects_60s > t.max_rejects_60s) return false;
    if (f.cancels_60s > t.max_cancels_60s) return false;
    if (f.disconnects > t.max_disconnects) return false;
    if (f.errors_60s > t.max_errors_60s) return false;
    
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Failure Governor (with Progressive Backoff)
// ─────────────────────────────────────────────────────────────────────────────
class FailureGovernor {
public:
    FailureGovernor() = default;
    
    void recordReject(uint64_t now_ns) {
        maybeResetWindow(now_ns);
        state_.rejects_60s++;
        state_.last_reject_ns = now_ns;
        checkAndTriggerBackoff(now_ns);
    }
    
    void recordCancel(uint64_t now_ns) {
        maybeResetWindow(now_ns);
        state_.cancels_60s++;
    }
    
    void recordDisconnect() {
        state_.disconnects++;
        backoff_.consecutive_triggers++;
        escalateBackoff();
    }
    
    void recordReconnect() {
        if (state_.disconnects > 0) state_.disconnects--;
    }
    
    void recordError(uint64_t now_ns) {
        maybeResetWindow(now_ns);
        state_.errors_60s++;
        state_.last_error_ns = now_ns;
        checkAndTriggerBackoff(now_ns);
    }
    
    bool allowed(uint64_t now_ns) const {
        // Check backoff first
        if (now_ns < backoff_.backoff_until_ns) {
            return false;
        }
        return tradingAllowed(state_, now_ns, thresh_);
    }
    
    bool inCooldown(uint64_t now_ns) const {
        return now_ns < state_.cooldown_until_ns || now_ns < backoff_.backoff_until_ns;
    }
    
    uint64_t cooldownRemaining(uint64_t now_ns) const {
        uint64_t max_until = std::max(state_.cooldown_until_ns, backoff_.backoff_until_ns);
        if (now_ns >= max_until) return 0;
        return max_until - now_ns;
    }
    
    BackoffTier currentBackoffTier() const { return backoff_.tier; }
    
    const FailureState& state() const { return state_; }
    const BackoffState& backoffState() const { return backoff_; }
    
    void setThresholds(const FailureThresholds& t) { thresh_ = t; }
    
    void reset() {
        state_ = FailureState{};
        backoff_ = BackoffState{};
    }
    
    // Manual recovery from HALT
    void manualRecovery() {
        if (backoff_.tier == BackoffTier::HALT) {
            backoff_.tier = BackoffTier::NONE;
            backoff_.backoff_until_ns = 0;
            backoff_.consecutive_triggers = 0;
        }
    }
    
private:
    FailureState state_;
    FailureThresholds thresh_;
    BackoffState backoff_;
    uint64_t window_start_ns_ = 0;
    
    void maybeResetWindow(uint64_t now_ns) {
        if (now_ns - window_start_ns_ > thresh_.window_ns) {
            state_.rejects_60s = 0;
            state_.cancels_60s = 0;
            state_.errors_60s = 0;
            window_start_ns_ = now_ns;
            
            // Decay consecutive triggers if window is clean
            if (backoff_.consecutive_triggers > 0) {
                backoff_.consecutive_triggers--;
            }
        }
    }
    
    void checkAndTriggerBackoff(uint64_t now_ns) {
        if (!tradingAllowed(state_, now_ns, thresh_)) {
            backoff_.consecutive_triggers++;
            escalateBackoff();
            backoff_.backoff_until_ns = now_ns + backoff_.backoffDuration();
            state_.cooldown_until_ns = backoff_.backoff_until_ns;
        }
    }
    
    void escalateBackoff() {
        // Progressive escalation based on consecutive triggers
        if (backoff_.consecutive_triggers >= 5) {
            backoff_.tier = BackoffTier::HALT;
        } else if (backoff_.consecutive_triggers >= 4) {
            backoff_.tier = BackoffTier::SEVERE;
        } else if (backoff_.consecutive_triggers >= 3) {
            backoff_.tier = BackoffTier::HEAVY;
        } else if (backoff_.consecutive_triggers >= 2) {
            backoff_.tier = BackoffTier::MODERATE;
        } else {
            backoff_.tier = BackoffTier::LIGHT;
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-Venue Failure Governor
// ─────────────────────────────────────────────────────────────────────────────
class VenueFailureGovernor {
public:
    static constexpr size_t MAX_VENUES = 8;
    
    void recordReject(const char* venue, uint64_t now_ns) {
        getOrCreate(venue).recordReject(now_ns);
    }
    
    void recordCancel(const char* venue, uint64_t now_ns) {
        getOrCreate(venue).recordCancel(now_ns);
    }
    
    void recordDisconnect(const char* venue) {
        getOrCreate(venue).recordDisconnect();
    }
    
    void recordError(const char* venue, uint64_t now_ns) {
        getOrCreate(venue).recordError(now_ns);
    }
    
    bool allowed(const char* venue, uint64_t now_ns) const {
        for (size_t i = 0; i < count_; i++) {
            if (strcmp(venues_[i].name, venue) == 0) {
                return venues_[i].governor.allowed(now_ns);
            }
        }
        return true;  // Unknown venue, allow
    }
    
    BackoffTier getBackoffTier(const char* venue) const {
        for (size_t i = 0; i < count_; i++) {
            if (strcmp(venues_[i].name, venue) == 0) {
                return venues_[i].governor.currentBackoffTier();
            }
        }
        return BackoffTier::NONE;
    }
    
private:
    struct VenueState {
        char name[32] = {0};
        FailureGovernor governor;
    };
    
    VenueState venues_[MAX_VENUES];
    size_t count_ = 0;
    
    FailureGovernor& getOrCreate(const char* venue) {
        for (size_t i = 0; i < count_; i++) {
            if (strcmp(venues_[i].name, venue) == 0) {
                return venues_[i].governor;
            }
        }
        if (count_ < MAX_VENUES) {
            strncpy(venues_[count_].name, venue, 31);
            return venues_[count_++].governor;
        }
        return venues_[0].governor;  // Fallback
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// High Frequency Risk Governor (for 1K+ fills/day)
// ─────────────────────────────────────────────────────────────────────────────
struct HFStats {
    uint32_t fills_today = 0;
    double pnl_today = 0.0;
    double pnl_peak = 0.0;
    uint32_t orders_today = 0;
    uint32_t rejects_today = 0;
};

struct HFLimits {
    uint32_t max_fills_per_day = 1200;
    uint32_t max_orders_per_day = 5000;
    double max_drawdown_from_peak_pct = 60.0;  // 60% of peak PnL
};

inline bool hfTradingAllowed(
    const HFStats& s,
    const HFLimits& limits = HFLimits{}
) {
    // Fill limit
    if (s.fills_today > limits.max_fills_per_day) return false;
    
    // Order limit
    if (s.orders_today > limits.max_orders_per_day) return false;
    
    // Drawdown from peak
    if (s.pnl_peak > 0) {
        double drawdown_pct = (s.pnl_peak - s.pnl_today) / s.pnl_peak * 100.0;
        if (drawdown_pct > limits.max_drawdown_from_peak_pct) return false;
    }
    
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Global Failure Governor Singleton
// ─────────────────────────────────────────────────────────────────────────────
inline FailureGovernor& getFailureGovernor() {
    static FailureGovernor gov;
    return gov;
}

} // namespace Risk
} // namespace Chimera
