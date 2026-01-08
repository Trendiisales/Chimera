// =============================================================================
// GoldPyramiding.hpp - Gold-Specific Pyramiding Logic (v4.6.0)
// =============================================================================
// PURPOSE: Pyramiding is NOT allowed globally
//          It is GOLD + BURST regime + NY session ONLY
//
// CONDITIONS (ALL MUST PASS):
//   - Symbol == XAUUSD
//   - Regime == BURST
//   - Session == NY
//   - q75 widening (upside expanding)
//   - latency < 120us
//   - existing position profitable
//
// This prevents death-by-pyramiding while exploiting speed bursts.
// =============================================================================
#pragma once

#include "MLModel.hpp"
#include <cstdio>
#include <atomic>
#include <cstring>

namespace Chimera {
namespace ML {

// =============================================================================
// Gold Pyramiding Configuration
// =============================================================================
struct GoldPyramidConfig {
    double min_open_pnl = 0.5;              // Minimum PnL before pyramid allowed
    double min_q75_expansion = 0.6;         // q75 - q50 must exceed this
    double max_latency_us = 120.0;          // Maximum latency for pyramid
    double max_pyramid_size_mult = 0.5;     // Pyramid size as fraction of initial
    int max_pyramid_levels = 2;             // Maximum number of pyramids
    double min_price_move_pct = 0.05;       // Minimum favorable move before pyramid
};

// =============================================================================
// Gold Pyramiding Guard
// =============================================================================
class GoldPyramidGuard {
public:
    GoldPyramidGuard() noexcept : config_{} {}
    explicit GoldPyramidGuard(const GoldPyramidConfig& cfg) noexcept : config_(cfg) {}
    
    // =========================================================================
    // Check if pyramid is allowed
    // =========================================================================
    struct PyramidResult {
        bool allowed = false;
        double size_mult = 0.0;
        const char* reject_reason = nullptr;
    };
    
    PyramidResult checkPyramid(
        const char* symbol,
        Regime regime,
        Session session,
        double open_pnl,
        const MLQuantiles& q,
        double latency_us,
        int current_pyramid_level,
        double entry_price,
        double current_price,
        bool venue_is_fix = true  // Pyramiding requires FIX venue
    ) noexcept {
        PyramidResult result;
        
        // =====================================================================
        // CHECK 0: Venue must be FIX (pyramiding never overrides venue downgrade)
        // =====================================================================
        if (!venue_is_fix) {
            result.reject_reason = "VENUE_NOT_FIX";
            rejects_venue_.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        
        // =====================================================================
        // CHECK 1: Must be XAUUSD
        // =====================================================================
        if (std::strcmp(symbol, "XAUUSD") != 0) {
            result.reject_reason = "NOT_GOLD";
            rejects_symbol_.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        
        // =====================================================================
        // CHECK 2: Must be BURST regime
        // =====================================================================
        if (regime != Regime::BURST) {
            result.reject_reason = "NOT_BURST_REGIME";
            rejects_regime_.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        
        // =====================================================================
        // CHECK 3: Must be NY session
        // =====================================================================
        if (session != Session::NY) {
            result.reject_reason = "NOT_NY_SESSION";
            rejects_session_.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        
        // =====================================================================
        // CHECK 4: Position must be profitable
        // =====================================================================
        if (open_pnl < config_.min_open_pnl) {
            result.reject_reason = "POSITION_NOT_PROFITABLE";
            rejects_pnl_.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        
        // =====================================================================
        // CHECK 5: q75 must be expanding (upside widening)
        // =====================================================================
        double q75_expansion = q.q75 - q.q50;
        if (q75_expansion < config_.min_q75_expansion) {
            result.reject_reason = "Q75_NOT_EXPANDING";
            rejects_q75_.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        
        // =====================================================================
        // CHECK 6: Latency must be excellent
        // =====================================================================
        if (latency_us > config_.max_latency_us) {
            result.reject_reason = "LATENCY_TOO_HIGH";
            rejects_latency_.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        
        // =====================================================================
        // CHECK 7: Haven't exceeded pyramid levels
        // =====================================================================
        if (current_pyramid_level >= config_.max_pyramid_levels) {
            result.reject_reason = "MAX_PYRAMIDS_REACHED";
            rejects_levels_.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        
        // =====================================================================
        // CHECK 8: Price has moved favorably
        // =====================================================================
        double price_move_pct = std::abs(current_price - entry_price) / entry_price * 100.0;
        if (price_move_pct < config_.min_price_move_pct) {
            result.reject_reason = "PRICE_NOT_MOVED_ENOUGH";
            rejects_price_.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        
        // =====================================================================
        // ALL CHECKS PASSED - Pyramid allowed
        // =====================================================================
        
        // Size decreases with each pyramid level
        double level_mult = 1.0 / (current_pyramid_level + 2);  // 1/2, 1/3, 1/4...
        result.size_mult = config_.max_pyramid_size_mult * level_mult;
        result.allowed = true;
        result.reject_reason = nullptr;
        
        accepts_.fetch_add(1, std::memory_order_relaxed);
        
        return result;
    }
    
    // =========================================================================
    // Stats
    // =========================================================================
    struct Stats {
        uint64_t accepts = 0;
        uint64_t rejects_venue = 0;
        uint64_t rejects_symbol = 0;
        uint64_t rejects_regime = 0;
        uint64_t rejects_session = 0;
        uint64_t rejects_pnl = 0;
        uint64_t rejects_q75 = 0;
        uint64_t rejects_latency = 0;
        uint64_t rejects_levels = 0;
        uint64_t rejects_price = 0;
        
        uint64_t total() const { return accepts + rejects_venue + rejects_symbol + 
            rejects_regime + rejects_session + rejects_pnl + rejects_q75 + 
            rejects_latency + rejects_levels + rejects_price; }
    };
    
    Stats getStats() const {
        Stats s;
        s.accepts = accepts_.load();
        s.rejects_venue = rejects_venue_.load();
        s.rejects_symbol = rejects_symbol_.load();
        s.rejects_regime = rejects_regime_.load();
        s.rejects_session = rejects_session_.load();
        s.rejects_pnl = rejects_pnl_.load();
        s.rejects_q75 = rejects_q75_.load();
        s.rejects_latency = rejects_latency_.load();
        s.rejects_levels = rejects_levels_.load();
        s.rejects_price = rejects_price_.load();
        return s;
    }
    
    void printStats() const {
        auto s = getStats();
        std::printf("[GoldPyramid] accepts=%lu | rejects: venue=%lu sym=%lu reg=%lu sess=%lu pnl=%lu q75=%lu lat=%lu lvl=%lu price=%lu\n",
            (unsigned long)s.accepts,
            (unsigned long)s.rejects_venue,
            (unsigned long)s.rejects_symbol, (unsigned long)s.rejects_regime,
            (unsigned long)s.rejects_session, (unsigned long)s.rejects_pnl,
            (unsigned long)s.rejects_q75, (unsigned long)s.rejects_latency,
            (unsigned long)s.rejects_levels, (unsigned long)s.rejects_price);
    }
    
    const GoldPyramidConfig& config() const { return config_; }
    
private:
    GoldPyramidConfig config_;
    
    std::atomic<uint64_t> accepts_{0};
    std::atomic<uint64_t> rejects_venue_{0};
    std::atomic<uint64_t> rejects_symbol_{0};
    std::atomic<uint64_t> rejects_regime_{0};
    std::atomic<uint64_t> rejects_session_{0};
    std::atomic<uint64_t> rejects_pnl_{0};
    std::atomic<uint64_t> rejects_q75_{0};
    std::atomic<uint64_t> rejects_latency_{0};
    std::atomic<uint64_t> rejects_levels_{0};
    std::atomic<uint64_t> rejects_price_{0};
};

// =============================================================================
// Global Gold Pyramid Guard
// =============================================================================
inline GoldPyramidGuard& getGoldPyramidGuard() {
    static GoldPyramidGuard instance;
    return instance;
}

} // namespace ML
} // namespace Chimera
