// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/binance/DeltaGate.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔒 LOCKED
// PURPOSE: Atomic execution gate driven by microstructure stress
// OWNER: Jo
// LAST VERIFIED: 2024-12-22
//
// DESIGN:
// - Lock-free state transitions
// - Deterministic behavior
// - Hot-path safe (no allocation, no locks, no syscalls)
// - Single source of truth for execution permission
//
// STATES:
// - ALLOW:    Normal trading, all intents pass
// - THROTTLE: Elevated stress, reduce position size / frequency
// - BLOCK:    High stress, no new intents allowed
//
// USAGE:
// - MicrostructureEngine sets state based on: vol burst, VPIN, liquidity gaps
// - Strategies read state before emitting intents
// - ExecutionGate checks before sending orders
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <atomic>
#include <cstdint>

namespace Chimera {
namespace Binance {

class DeltaGate {
public:
    enum State : uint8_t {
        ALLOW    = 0,  // Normal trading
        THROTTLE = 1,  // Reduce size/frequency
        BLOCK    = 2   // No new trades
    };

    DeltaGate() noexcept : state_(ALLOW) {}

    // ═══════════════════════════════════════════════════════════════════════
    // STATE SETTERS (called by microstructure engine)
    // ═══════════════════════════════════════════════════════════════════════
    
    inline void set_allow() noexcept { 
        state_.store(ALLOW, std::memory_order_release); 
    }
    
    inline void set_throttle() noexcept { 
        state_.store(THROTTLE, std::memory_order_release); 
    }
    
    inline void set_block() noexcept { 
        state_.store(BLOCK, std::memory_order_release); 
    }
    
    // Set state based on stress level (0.0 = calm, 1.0 = extreme)
    inline void set_from_stress(double stress) noexcept {
        if (stress >= 0.8) {
            set_block();
        } else if (stress >= 0.5) {
            set_throttle();
        } else {
            set_allow();
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // STATE READERS (hot path - called by strategies/execution)
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] inline State state() const noexcept {
        return static_cast<State>(state_.load(std::memory_order_acquire));
    }

    [[nodiscard]] inline bool can_trade() const noexcept {
        return state() == ALLOW;
    }

    [[nodiscard]] inline bool should_throttle() const noexcept {
        return state() == THROTTLE;
    }
    
    [[nodiscard]] inline bool is_blocked() const noexcept {
        return state() == BLOCK;
    }
    
    // Get multiplier for position sizing (1.0 = full, 0.5 = half, 0.0 = none)
    [[nodiscard]] inline double size_multiplier() const noexcept {
        switch (state()) {
            case ALLOW:    return 1.0;
            case THROTTLE: return 0.5;
            case BLOCK:    return 0.0;
        }
        return 0.0;
    }

private:
    alignas(64) std::atomic<uint8_t> state_;  // Cache-line aligned
};

} // namespace Binance
} // namespace Chimera
