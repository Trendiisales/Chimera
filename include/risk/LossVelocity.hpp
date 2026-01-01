// =============================================================================
// LossVelocity.hpp - v4.8.0 - LOSS VELOCITY TRACKER
// =============================================================================
// PURPOSE: Adaptive cooldown based on loss clustering
//
// WHAT IT DOES:
//   Cooldown duration increases automatically when losses cluster.
//
// THIS AVOIDS:
//   - Revenge sequences
//   - Chop death
//   - Overtrading bad micro regimes
//
// RULE:
//   loss_velocity = losses in last 10 minutes
//   cooldown = base + (loss_velocity × multiplier)
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include <deque>
#include <cstdint>
#include <cstdio>

namespace Chimera {

class LossVelocity {
public:
    // =========================================================================
    // CONFIGURATION
    // =========================================================================
    static constexpr uint64_t WINDOW_NS = 600'000'000'000ULL;  // 10 minutes
    static constexpr uint64_t BASE_COOLDOWN_NS = 5'000'000'000ULL;  // 5 seconds
    static constexpr uint64_t COOLDOWN_PER_LOSS_NS = 3'000'000'000ULL;  // +3s per loss
    static constexpr int MAX_COOLDOWN_LOSSES = 5;  // Cap at 5 losses (20s max cooldown)
    
    // =========================================================================
    // RECORD A LOSS
    // =========================================================================
    void recordLoss(uint64_t ts_ns) {
        losses_.push_back(ts_ns);
        prune(ts_ns);
    }
    
    // =========================================================================
    // GET LOSS COUNT IN WINDOW
    // =========================================================================
    int count(uint64_t now_ns) {
        prune(now_ns);
        return static_cast<int>(losses_.size());
    }
    
    // =========================================================================
    // GET ADAPTIVE COOLDOWN
    // =========================================================================
    uint64_t getAdaptiveCooldown(uint64_t now_ns) {
        int velocity = count(now_ns);
        int cappedVelocity = (velocity > MAX_COOLDOWN_LOSSES) ? MAX_COOLDOWN_LOSSES : velocity;
        return BASE_COOLDOWN_NS + (static_cast<uint64_t>(cappedVelocity) * COOLDOWN_PER_LOSS_NS);
    }
    
    // =========================================================================
    // CHECK IF IN COOLDOWN
    // =========================================================================
    bool inCooldown(uint64_t now_ns, uint64_t lastTradeEnd_ns) const {
        if (lastTradeEnd_ns == 0) return false;
        
        // Get current loss count (without modifying state)
        int velocity = 0;
        for (const auto& ts : losses_) {
            if (now_ns - ts <= WINDOW_NS) velocity++;
        }
        
        int cappedVelocity = (velocity > MAX_COOLDOWN_LOSSES) ? MAX_COOLDOWN_LOSSES : velocity;
        uint64_t cooldown = BASE_COOLDOWN_NS + (static_cast<uint64_t>(cappedVelocity) * COOLDOWN_PER_LOSS_NS);
        
        return (now_ns - lastTradeEnd_ns) < cooldown;
    }
    
    // =========================================================================
    // RESET
    // =========================================================================
    void reset() {
        losses_.clear();
    }
    
    // =========================================================================
    // DEBUG OUTPUT
    // =========================================================================
    void print(uint64_t now_ns) const {
        int velocity = 0;
        for (const auto& ts : losses_) {
            if (now_ns - ts <= WINDOW_NS) velocity++;
        }
        
        int cappedVelocity = (velocity > MAX_COOLDOWN_LOSSES) ? MAX_COOLDOWN_LOSSES : velocity;
        uint64_t cooldown = BASE_COOLDOWN_NS + (static_cast<uint64_t>(cappedVelocity) * COOLDOWN_PER_LOSS_NS);
        
        printf("[LOSS-VELOCITY] Losses in 10min: %d | Cooldown: %.1fs\n",
               velocity, static_cast<double>(cooldown) / 1e9);
    }

private:
    std::deque<uint64_t> losses_;

    void prune(uint64_t now_ns) {
        while (!losses_.empty() && now_ns - losses_.front() > WINDOW_NS) {
            losses_.pop_front();
        }
    }
};

// =============================================================================
// CONSECUTIVE LOSS TRACKER
// =============================================================================
class ConsecutiveLossTracker {
public:
    static constexpr int MAX_CONSECUTIVE_LOSSES = 2;  // Auto-disable after 2
    
    void recordWin() {
        consecutiveLosses_ = 0;
    }
    
    void recordLoss() {
        consecutiveLosses_++;
    }
    
    int count() const {
        return consecutiveLosses_;
    }
    
    bool shouldDisable() const {
        return consecutiveLosses_ >= MAX_CONSECUTIVE_LOSSES;
    }
    
    void reset() {
        consecutiveLosses_ = 0;
    }
    
    void print() const {
        printf("[CONSECUTIVE-LOSS] Count: %d / %d %s\n",
               consecutiveLosses_, MAX_CONSECUTIVE_LOSSES,
               shouldDisable() ? "⚠️ DISABLE" : "✔");
    }

private:
    int consecutiveLosses_ = 0;
};

} // namespace Chimera
