// ═══════════════════════════════════════════════════════════════════════════════
// include/execution/MicroRepost.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.11: MICRO CANCEL/REPOST LOOP (PHYSICS-GATED + THROTTLE-AWARE)
//
// PURPOSE: Fast cancel/repost of maker orders when price moves.
// Only enabled when ExecCapabilities.allow_micro_repost == true.
//
// DANGER: This is extremely dangerous on WAN latency.
// Racing against your own cancel can cause double fills.
//
// THROTTLE AWARENESS:
// - Tracks venue throttle state
// - Disables repost when approaching rate limits
// - Prevents account bans
//
// TIMING:
// - COLO: Repost after 1.2ms
// - NEAR_COLO: Repost after 2.5ms
// - WAN: DISABLED (would race against self)
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstring>
#include <chrono>
#include "runtime/ExecutionCapabilities.hpp"

namespace Chimera {
namespace Execution {

// ─────────────────────────────────────────────────────────────────────────────
// Venue Throttle State
// ─────────────────────────────────────────────────────────────────────────────
enum class ThrottleLevel : uint8_t {
    SAFE       = 0,  // No throttle concerns
    CAUTION    = 1,  // Approaching limits, reduce rate
    WARNING    = 2,  // Near limits, minimal operations
    CRITICAL   = 3   // At limits, no new operations
};

struct VenueThrottle {
    char venue[32] = {0};
    ThrottleLevel level = ThrottleLevel::SAFE;
    
    // Rate tracking
    uint32_t orders_last_second = 0;
    uint32_t cancels_last_second = 0;
    uint32_t orders_last_minute = 0;
    uint64_t last_reset_ns = 0;
    
    // Limits (venue-specific)
    uint32_t max_orders_per_second = 10;
    uint32_t max_cancels_per_second = 20;
    uint32_t max_orders_per_minute = 300;
};

inline ThrottleLevel computeThrottleLevel(const VenueThrottle& t) {
    double order_usage = static_cast<double>(t.orders_last_second) / t.max_orders_per_second;
    double cancel_usage = static_cast<double>(t.cancels_last_second) / t.max_cancels_per_second;
    double minute_usage = static_cast<double>(t.orders_last_minute) / t.max_orders_per_minute;
    
    double max_usage = std::max({order_usage, cancel_usage, minute_usage});
    
    if (max_usage >= 0.95) return ThrottleLevel::CRITICAL;
    if (max_usage >= 0.80) return ThrottleLevel::WARNING;
    if (max_usage >= 0.60) return ThrottleLevel::CAUTION;
    return ThrottleLevel::SAFE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Repost Configuration
// ─────────────────────────────────────────────────────────────────────────────
struct MicroRepostConfig {
    double colo_repost_ms = 1.2;        // Repost after this time in COLO
    double near_colo_repost_ms = 2.5;   // Repost after this time in NEAR_COLO
    double price_move_threshold = 0.5;  // Ticks of price movement to trigger
    int max_reposts = 3;                // Max reposts before giving up
};

// ─────────────────────────────────────────────────────────────────────────────
// Repost State
// ─────────────────────────────────────────────────────────────────────────────
struct RepostState {
    int repost_count = 0;
    double original_price = 0.0;
    double current_price = 0.0;
    uint64_t order_sent_ns = 0;
    bool pending_cancel = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Should Repost? (Throttle-Aware)
// ─────────────────────────────────────────────────────────────────────────────
inline bool shouldRepost(
    double elapsed_ms,
    const Runtime::ExecCapabilities& cap,
    ThrottleLevel throttle = ThrottleLevel::SAFE,
    const MicroRepostConfig& cfg = MicroRepostConfig{}
) {
    // CRITICAL: Throttle override
    if (throttle >= ThrottleLevel::WARNING) {
        return false;  // Never repost when throttled
    }
    
    // Disabled if physics doesn't allow
    if (!cap.allow_micro_repost) {
        return false;
    }
    
    // Reduce repost frequency if approaching throttle
    double timing_multiplier = (throttle == ThrottleLevel::CAUTION) ? 2.0 : 1.0;
    
    // Check timing based on physics
    switch (cap.physics) {
        case Runtime::ExecPhysics::COLO:
            return elapsed_ms > cfg.colo_repost_ms * timing_multiplier;
            
        case Runtime::ExecPhysics::NEAR_COLO:
            return elapsed_ms > cfg.near_colo_repost_ms * timing_multiplier;
            
        default:
            return false;  // Never repost on WAN
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Should Repost Due to Price Movement?
// ─────────────────────────────────────────────────────────────────────────────
inline bool shouldRepostOnPriceMove(
    double order_price,
    double current_mid,
    double tick_size,
    const Runtime::ExecCapabilities& cap,
    ThrottleLevel throttle = ThrottleLevel::SAFE,
    const MicroRepostConfig& cfg = MicroRepostConfig{}
) {
    if (!cap.allow_micro_repost) {
        return false;
    }
    
    // Never repost when throttled
    if (throttle >= ThrottleLevel::WARNING) {
        return false;
    }
    
    // Increase threshold when approaching throttle
    double threshold = cfg.price_move_threshold;
    if (throttle == ThrottleLevel::CAUTION) {
        threshold *= 2.0;  // Only repost on larger moves
    }
    
    // Price moved away from us
    double ticks_moved = std::abs(current_mid - order_price) / tick_size;
    return ticks_moved > threshold;
}

// ─────────────────────────────────────────────────────────────────────────────
// Repost Decision
// ─────────────────────────────────────────────────────────────────────────────
struct RepostDecision {
    bool should_repost = false;
    bool should_give_up = false;
    double new_price = 0.0;
    const char* reason = "NONE";
};

inline RepostDecision makeRepostDecision(
    const RepostState& state,
    double elapsed_ms,
    double current_mid,
    double tick_size,
    const Runtime::ExecCapabilities& cap,
    ThrottleLevel throttle = ThrottleLevel::SAFE,
    const MicroRepostConfig& cfg = MicroRepostConfig{}
) {
    RepostDecision dec;
    
    // Throttle check first
    if (throttle >= ThrottleLevel::WARNING) {
        dec.reason = "THROTTLED";
        return dec;
    }
    
    // Already at max reposts?
    if (state.repost_count >= cfg.max_reposts) {
        dec.should_give_up = true;
        dec.reason = "MAX_REPOSTS";
        return dec;
    }
    
    // Pending cancel - don't repost yet
    if (state.pending_cancel) {
        dec.reason = "PENDING_CANCEL";
        return dec;
    }
    
    // Check time-based repost
    if (shouldRepost(elapsed_ms, cap, throttle, cfg)) {
        dec.should_repost = true;
        dec.new_price = current_mid;  // Repost at current mid
        dec.reason = "TIME_ELAPSED";
        return dec;
    }
    
    // Check price-movement repost
    if (shouldRepostOnPriceMove(state.original_price, current_mid, tick_size, cap, throttle, cfg)) {
        dec.should_repost = true;
        dec.new_price = current_mid;
        dec.reason = "PRICE_MOVED";
        return dec;
    }
    
    dec.reason = "WAIT";
    return dec;
}

// ─────────────────────────────────────────────────────────────────────────────
// Throttle Manager (tracks per-venue throttle state)
// ─────────────────────────────────────────────────────────────────────────────
class ThrottleManager {
public:
    static constexpr size_t MAX_VENUES = 8;
    
    void recordOrder(const char* venue, uint64_t now_ns) {
        VenueThrottle* t = findOrCreate(venue);
        if (!t) return;
        maybeReset(t, now_ns);
        t->orders_last_second++;
        t->orders_last_minute++;
        t->level = computeThrottleLevel(*t);
    }
    
    void recordCancel(const char* venue, uint64_t now_ns) {
        VenueThrottle* t = findOrCreate(venue);
        if (!t) return;
        maybeReset(t, now_ns);
        t->cancels_last_second++;
        t->level = computeThrottleLevel(*t);
    }
    
    ThrottleLevel getLevel(const char* venue) const {
        for (size_t i = 0; i < count_; i++) {
            if (strcmp(venues_[i].venue, venue) == 0) {
                return venues_[i].level;
            }
        }
        return ThrottleLevel::SAFE;
    }
    
    void setLimits(const char* venue, uint32_t orders_sec, uint32_t cancels_sec, uint32_t orders_min) {
        VenueThrottle* t = findOrCreate(venue);
        if (!t) return;
        t->max_orders_per_second = orders_sec;
        t->max_cancels_per_second = cancels_sec;
        t->max_orders_per_minute = orders_min;
    }
    
private:
    VenueThrottle venues_[MAX_VENUES];
    size_t count_ = 0;
    
    VenueThrottle* findOrCreate(const char* venue) {
        for (size_t i = 0; i < count_; i++) {
            if (strcmp(venues_[i].venue, venue) == 0) {
                return &venues_[i];
            }
        }
        if (count_ < MAX_VENUES) {
            strncpy(venues_[count_].venue, venue, 31);
            return &venues_[count_++];
        }
        return nullptr;
    }
    
    void maybeReset(VenueThrottle* t, uint64_t now_ns) {
        constexpr uint64_t ONE_SECOND_NS = 1'000'000'000ULL;
        constexpr uint64_t ONE_MINUTE_NS = 60'000'000'000ULL;
        
        if (now_ns - t->last_reset_ns > ONE_SECOND_NS) {
            t->orders_last_second = 0;
            t->cancels_last_second = 0;
            
            if (now_ns - t->last_reset_ns > ONE_MINUTE_NS) {
                t->orders_last_minute = 0;
            }
            
            t->last_reset_ns = now_ns;
        }
    }
};

inline ThrottleManager& getThrottleManager() {
    static ThrottleManager mgr;
    return mgr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Get Repost Timeout for Physics
// ─────────────────────────────────────────────────────────────────────────────
inline double getRepostTimeout(
    Runtime::ExecPhysics physics,
    const MicroRepostConfig& cfg = MicroRepostConfig{}
) {
    switch (physics) {
        case Runtime::ExecPhysics::COLO:
            return cfg.colo_repost_ms;
        case Runtime::ExecPhysics::NEAR_COLO:
            return cfg.near_colo_repost_ms;
        default:
            return 1000000.0;  // Effectively infinite (never repost)
    }
}

} // namespace Execution
} // namespace Chimera
