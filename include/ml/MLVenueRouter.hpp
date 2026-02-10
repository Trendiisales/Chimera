// =============================================================================
// MLVenueRouter.hpp - ML-Aware Venue Selection (v4.6.0)
// =============================================================================
// PURPOSE: Route orders to safest venue based on ML risk metrics
//
// POLICY:
//   - High tail risk (absolute) → CFD
//   - High tail spread (relative) → CFD (catches regime stress early)
//   - Tight distribution → FIX (tighter fills, speed matters)
//   - Latency spike → CFD fallback (avoid FIX slippage explosion)
//
// This prevents FIX slippage explosions during stress.
// =============================================================================
#pragma once

#include "MLModel.hpp"
#include <atomic>
#include <cstdio>

namespace Chimera {
namespace ML {

// =============================================================================
// Venue Types
// =============================================================================
enum class Venue : uint8_t {
    FIX = 0,   // Direct FIX connection (tighter, faster, riskier)
    CFD = 1    // CFD broker (wider, safer, slower)
};

inline const char* venueToStr(Venue v) {
    return v == Venue::FIX ? "FIX" : "CFD";
}

// =============================================================================
// Venue Routing Configuration
// =============================================================================
struct VenueRoutingConfig {
    // Absolute tail risk threshold
    double tail_risk_threshold = -1.5;     // q10 below this → CFD
    
    // Relative tail spread threshold (catches regime stress)
    // Route to CFD if (q50 - q10) > tail_spread_threshold
    double tail_spread_threshold = 2.5;    // Tail spread above this → CFD
    
    // Other thresholds
    double latency_fallback_us = 220.0;    // Latency above this → CFD
    double min_iqr_for_fix = 0.3;          // IQR below this → CFD (distribution too tight)
    double spread_widen_threshold = 2.0;   // Spread z-score above this → CFD
};

// =============================================================================
// ML Venue Router
// =============================================================================
class MLVenueRouter {
public:
    MLVenueRouter() noexcept : config_{} {}
    explicit MLVenueRouter(const VenueRoutingConfig& cfg) noexcept : config_(cfg) {}
    
    // =========================================================================
    // Select Venue Based on ML Risk Metrics
    // =========================================================================
    struct RouteResult {
        Venue venue = Venue::CFD;
        const char* reason = nullptr;
    };
    
    RouteResult selectVenue(
        const MLQuantiles& q,
        double latency_us,
        double spread_z = 0.0  // Optional spread z-score
    ) noexcept {
        RouteResult result;
        
        // =====================================================================
        // CHECK 1: Tail risk too high (ABSOLUTE) → CFD
        // =====================================================================
        if (q.q10 < config_.tail_risk_threshold) {
            result.venue = Venue::CFD;
            result.reason = "TAIL_RISK_HIGH";
            cfd_tail_.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        
        // =====================================================================
        // CHECK 2: Tail spread too wide (RELATIVE) → CFD
        // This catches regime stress even when absolute q10 looks OK
        // Example: q10=-1.2 is "ok" but if q50=0.3, tail spread is 1.5
        //          That's a lot of downside risk relative to expectancy
        // =====================================================================
        double tail_spread = q.tailSpread();
        if (tail_spread > config_.tail_spread_threshold) {
            result.venue = Venue::CFD;
            result.reason = "TAIL_SPREAD_WIDE";
            cfd_tail_spread_.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        
        // =====================================================================
        // CHECK 3: Latency spike → CFD fallback
        // =====================================================================
        if (latency_us > config_.latency_fallback_us) {
            result.venue = Venue::CFD;
            result.reason = "LATENCY_SPIKE";
            cfd_latency_.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        
        // =====================================================================
        // CHECK 4: Distribution too tight → CFD (not worth FIX risk)
        // =====================================================================
        double iqr = q.q75 - q.q25;
        if (iqr < config_.min_iqr_for_fix) {
            result.venue = Venue::CFD;
            result.reason = "IQR_TOO_TIGHT";
            cfd_iqr_.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        
        // =====================================================================
        // CHECK 5: Spread widening → CFD
        // =====================================================================
        if (spread_z > config_.spread_widen_threshold) {
            result.venue = Venue::CFD;
            result.reason = "SPREAD_WIDE";
            cfd_spread_.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        
        // =====================================================================
        // DEFAULT: FIX (tighter execution)
        // =====================================================================
        result.venue = Venue::FIX;
        result.reason = nullptr;
        fix_routed_.fetch_add(1, std::memory_order_relaxed);
        
        return result;
    }
    
    // =========================================================================
    // Stats
    // =========================================================================
    struct Stats {
        uint64_t fix_routed = 0;
        uint64_t cfd_tail = 0;
        uint64_t cfd_tail_spread = 0;
        uint64_t cfd_latency = 0;
        uint64_t cfd_iqr = 0;
        uint64_t cfd_spread = 0;
        
        uint64_t total_cfd() const { 
            return cfd_tail + cfd_tail_spread + cfd_latency + cfd_iqr + cfd_spread; 
        }
        uint64_t total() const { return fix_routed + total_cfd(); }
        double fix_pct() const { 
            uint64_t t = total();
            return t > 0 ? (100.0 * fix_routed / t) : 0.0;
        }
    };
    
    Stats getStats() const {
        Stats s;
        s.fix_routed = fix_routed_.load();
        s.cfd_tail = cfd_tail_.load();
        s.cfd_tail_spread = cfd_tail_spread_.load();
        s.cfd_latency = cfd_latency_.load();
        s.cfd_iqr = cfd_iqr_.load();
        s.cfd_spread = cfd_spread_.load();
        return s;
    }
    
    void printStats() const {
        auto s = getStats();
        std::printf("[MLVenueRouter] FIX=%lu (%.1f%%) | CFD: tail=%lu tailspread=%lu lat=%lu iqr=%lu spread=%lu\n",
            (unsigned long)s.fix_routed, s.fix_pct(),
            (unsigned long)s.cfd_tail, (unsigned long)s.cfd_tail_spread,
            (unsigned long)s.cfd_latency, (unsigned long)s.cfd_iqr,
            (unsigned long)s.cfd_spread);
    }
    
    const VenueRoutingConfig& config() const { return config_; }
    
private:
    VenueRoutingConfig config_;
    
    std::atomic<uint64_t> fix_routed_{0};
    std::atomic<uint64_t> cfd_tail_{0};
    std::atomic<uint64_t> cfd_tail_spread_{0};
    std::atomic<uint64_t> cfd_latency_{0};
    std::atomic<uint64_t> cfd_iqr_{0};
    std::atomic<uint64_t> cfd_spread_{0};
};

// =============================================================================
// Global Venue Router
// =============================================================================
inline MLVenueRouter& getMLVenueRouter() {
    static MLVenueRouter instance;
    return instance;
}

} // namespace ML
} // namespace Chimera
