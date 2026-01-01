// ═══════════════════════════════════════════════════════════════════════════════
// include/core/ExecutionGate.hpp - IMMUTABLE CONTRACT
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔒 LOCKED
// PURPOSE: Per-symbol execution gating - decides if orders can be sent
// OWNER: Jo
// LAST VERIFIED: 2024-12-21
//
// DO NOT MODIFY WITHOUT EXPLICIT OWNER APPROVAL
//
// DESIGN:
// - Each symbol thread has its own ExecutionGate
// - Checks local state + shared atomics (GlobalKill, DailyLossGuard)
// - No locks, no allocation, pure reads
// - Returns allow/deny decision in < 1μs
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <atomic>
#include "core/GlobalKill.hpp"
#include "risk/DailyLossGuard.hpp"

namespace Chimera {

// ─────────────────────────────────────────────────────────────────────────────
// GateDecision - Result of execution gate check
// ─────────────────────────────────────────────────────────────────────────────
enum class GateReject : uint8_t {
    NONE            = 0,   // Allowed to trade
    GLOBAL_KILL     = 1,   // System killed
    DAILY_LOSS      = 2,   // Daily loss limit hit
    MAX_POSITION    = 3,   // Position limit reached
    MAX_ORDERS      = 4,   // Too many orders in flight
    COOLDOWN        = 5,   // Rate limiting
    VENUE_DOWN      = 6,   // Venue connection lost
    STALE_TICK      = 7,   // Tick data is stale
    LOW_CONFIDENCE  = 8,   // Strategy confidence too low
};

struct GateDecision {
    bool        allowed;
    GateReject  reason;
    
    [[nodiscard]] inline explicit operator bool() const noexcept {
        return allowed;
    }
    
    // Factory for quick allow/deny
    [[nodiscard]] static constexpr GateDecision allow() noexcept {
        return { true, GateReject::NONE };
    }
    
    [[nodiscard]] static constexpr GateDecision deny(GateReject r) noexcept {
        return { false, r };
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// ExecutionGate - Per-symbol execution gating
// ─────────────────────────────────────────────────────────────────────────────
// Each symbol thread owns one of these. It checks:
// 1. Global kill switch (shared atomic)
// 2. Daily loss guard (shared atomic)
// 3. Local position limits
// 4. Local rate limiting
// 5. Venue health
// ─────────────────────────────────────────────────────────────────────────────
class ExecutionGate {
public:
    // Configuration (set at startup, then read-only)
    // v6.88 FIX: Relaxed defaults for crypto HFT
    struct Config {
        double   max_position      = 1.0;       // Maximum position size
        uint32_t max_orders_flight = 5;         // v6.88: Was 3, now 5 for crypto
        uint64_t cooldown_ns       = 100'000'000; // v6.88: Was 250ms, now 100ms for crypto
        double   min_confidence    = 0.3;       // v6.88: Was 0.5, now 0.3 for crypto
        uint64_t stale_threshold_ns = 2'000'000'000; // v6.88: Was 1s, now 2s (network jitter)
    };
    
    // Factory for crypto-optimized config
    static Config cryptoConfig() noexcept {
        Config c;
        c.max_position = 0.5;
        c.max_orders_flight = 5;
        c.cooldown_ns = 100'000'000;  // 100ms
        c.min_confidence = 0.25;       // Very low for crypto
        c.stale_threshold_ns = 2'000'000'000;
        return c;
    }
    
    // Factory for CFD config
    static Config cfdConfig() noexcept {
        Config c;
        c.max_position = 0.1;
        c.max_orders_flight = 3;
        c.cooldown_ns = 500'000'000;  // 500ms
        c.min_confidence = 0.4;
        c.stale_threshold_ns = 1'000'000'000;
        return c;
    }
    
    ExecutionGate(
        const GlobalKill& kill,
        const DailyLossGuard& daily_loss,
        const Config& cfg
    ) noexcept
        : global_kill_(kill)
        , daily_loss_(daily_loss)
        , config_(cfg)
        , position_(0.0)
        , orders_in_flight_(0)
        , last_order_ts_ns_(0)
        , venue_up_(true)
    {}
    
    // ═══════════════════════════════════════════════════════════════════════
    // HOT PATH - Called before every potential order
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] inline GateDecision check(
        double confidence,
        uint64_t tick_ts_ns,
        uint64_t now_ns
    ) const noexcept {
        // 1. Global kill switch (shared atomic - relaxed read)
        if (global_kill_.killed()) [[unlikely]] {
            return GateDecision::deny(GateReject::GLOBAL_KILL);
        }
        
        // 2. Daily loss limit (shared atomic - relaxed read)
        if (!daily_loss_.allow()) [[unlikely]] {
            return GateDecision::deny(GateReject::DAILY_LOSS);
        }
        
        // 3. Venue health (local atomic - relaxed read)
        if (!venue_up_.load(std::memory_order_relaxed)) [[unlikely]] {
            return GateDecision::deny(GateReject::VENUE_DOWN);
        }
        
        // 4. Position limit (local - no atomic needed, single thread)
        if (position_ >= config_.max_position || 
            position_ <= -config_.max_position) [[unlikely]] {
            return GateDecision::deny(GateReject::MAX_POSITION);
        }
        
        // 5. Orders in flight limit
        if (orders_in_flight_.load(std::memory_order_relaxed) >= 
            config_.max_orders_flight) [[unlikely]] {
            return GateDecision::deny(GateReject::MAX_ORDERS);
        }
        
        // 6. Cooldown (rate limiting)
        if (now_ns - last_order_ts_ns_.load(std::memory_order_relaxed) < 
            config_.cooldown_ns) [[unlikely]] {
            return GateDecision::deny(GateReject::COOLDOWN);
        }
        
        // 7. Tick staleness
        if (now_ns - tick_ts_ns > config_.stale_threshold_ns) [[unlikely]] {
            return GateDecision::deny(GateReject::STALE_TICK);
        }
        
        // 8. Strategy confidence
        if (confidence < config_.min_confidence) [[unlikely]] {
            return GateDecision::deny(GateReject::LOW_CONFIDENCE);
        }
        
        return GateDecision::allow();
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // STATE UPDATES (called after order events)
    // ═══════════════════════════════════════════════════════════════════════
    
    // Called when order is sent
    inline void on_order_sent(uint64_t ts_ns) noexcept {
        orders_in_flight_.fetch_add(1, std::memory_order_relaxed);
        last_order_ts_ns_.store(ts_ns, std::memory_order_relaxed);
    }
    
    // Called when order is acknowledged (filled, rejected, or cancelled)
    inline void on_order_done() noexcept {
        orders_in_flight_.fetch_sub(1, std::memory_order_relaxed);
    }
    
    // Called on fill to update position
    inline void on_fill(double qty, Side side) noexcept {
        if (side == Side::Buy) {
            position_ += qty;
        } else if (side == Side::Sell) {
            position_ -= qty;
        }
    }
    
    // Called when venue connection state changes
    inline void set_venue_up(bool up) noexcept {
        venue_up_.store(up, std::memory_order_relaxed);
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // ACCESSORS (for metrics/logging)
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] double position() const noexcept { return position_; }
    [[nodiscard]] uint32_t orders_in_flight() const noexcept { 
        return orders_in_flight_.load(std::memory_order_relaxed); 
    }
    [[nodiscard]] bool venue_up() const noexcept { 
        return venue_up_.load(std::memory_order_relaxed); 
    }

private:
    // Shared state (read-only references)
    const GlobalKill& global_kill_;
    const DailyLossGuard& daily_loss_;
    
    // Configuration (set once at startup)
    const Config config_;
    
    // Local state (single-threaded access, no locks needed)
    double position_;  // Current position (not atomic - single thread)
    
    // These use atomics because they may be read by metrics thread
    alignas(64) std::atomic<uint32_t> orders_in_flight_;
    alignas(64) std::atomic<uint64_t> last_order_ts_ns_;
    alignas(64) std::atomic<bool>     venue_up_;
};

// ─────────────────────────────────────────────────────────────────────────────
// String conversion for logging (cold path)
// ─────────────────────────────────────────────────────────────────────────────
inline const char* to_string(GateReject r) noexcept {
    switch (r) {
        case GateReject::NONE:           return "ALLOWED";
        case GateReject::GLOBAL_KILL:    return "GLOBAL_KILL";
        case GateReject::DAILY_LOSS:     return "DAILY_LOSS";
        case GateReject::MAX_POSITION:   return "MAX_POSITION";
        case GateReject::MAX_ORDERS:     return "MAX_ORDERS";
        case GateReject::COOLDOWN:       return "COOLDOWN";
        case GateReject::VENUE_DOWN:     return "VENUE_DOWN";
        case GateReject::STALE_TICK:     return "STALE_TICK";
        case GateReject::LOW_CONFIDENCE: return "LOW_CONFIDENCE";
        default:                         return "UNKNOWN";
    }
}

} // namespace Chimera
