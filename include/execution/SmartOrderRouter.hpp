// ═══════════════════════════════════════════════════════════════════════════════
// include/execution/SmartOrderRouter.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.12.0: CRYPTO REMOVED - CFD only
//
// PURPOSE: Choose venue by fill probability × latency × economics.
// Automatically routes away from degraded brokers.
//
// SCORING:
// - Fill rate (higher = better)
// - Latency (lower = better)
// - Spread (lower = better)
// - Reject rate (lower = better)
//
// SUPPORTS:
// - BlackBull / Pepperstone / IC Markets (CFD via cTrader FIX/OpenAPI)
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>

namespace Chimera {
namespace Execution {

// ─────────────────────────────────────────────────────────────────────────────
// Venue Score
// ─────────────────────────────────────────────────────────────────────────────
struct VenueScore {
    char venue[32] = {0};
    double latency_ms = 0.0;
    double fill_rate = 0.0;       // 0-1
    double spread_bps = 0.0;
    double reject_rate = 0.0;     // 0-1
    double commission_bps = 0.0;
    bool available = true;
};

// ─────────────────────────────────────────────────────────────────────────────
// SOR Weights
// ─────────────────────────────────────────────────────────────────────────────
struct SORWeights {
    double fill_rate_weight = 100.0;    // Fill rate is king
    double latency_weight = 0.7;        // Latency penalty
    double spread_weight = 2.0;         // Spread penalty
    double reject_weight = 50.0;        // Reject penalty
    double commission_weight = 1.0;     // Commission penalty
};

// ─────────────────────────────────────────────────────────────────────────────
// Compute Venue Score (higher is better)
// ─────────────────────────────────────────────────────────────────────────────
inline double computeVenueScore(
    const VenueScore& v,
    const SORWeights& w = SORWeights{}
) {
    if (!v.available) return -1e9;
    
    return (v.fill_rate * w.fill_rate_weight)
         - (v.latency_ms * w.latency_weight)
         - (v.spread_bps * w.spread_weight)
         - (v.reject_rate * w.reject_weight)
         - (v.commission_bps * w.commission_weight);
}

// ─────────────────────────────────────────────────────────────────────────────
// Routing Decision
// ─────────────────────────────────────────────────────────────────────────────
struct RoutingDecision {
    char venue[32] = {0};
    double score = 0.0;
    double advantage_vs_second = 0.0;
    bool confident = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Choose Best Venue
// ─────────────────────────────────────────────────────────────────────────────
inline RoutingDecision chooseVenue(
    const std::vector<VenueScore>& venues,
    const SORWeights& w = SORWeights{}
) {
    RoutingDecision dec;
    
    if (venues.empty()) {
        strncpy(dec.venue, "NONE", 31);
        return dec;
    }
    
    double best_score = -1e9;
    double second_score = -1e9;
    size_t best_idx = 0;
    
    for (size_t i = 0; i < venues.size(); i++) {
        double score = computeVenueScore(venues[i], w);
        if (score > best_score) {
            second_score = best_score;
            best_score = score;
            best_idx = i;
        } else if (score > second_score) {
            second_score = score;
        }
    }
    
    strncpy(dec.venue, venues[best_idx].venue, 31);
    dec.score = best_score;
    dec.advantage_vs_second = best_score - second_score;
    dec.confident = dec.advantage_vs_second > 5.0;  // Clear winner
    
    return dec;
}

// ─────────────────────────────────────────────────────────────────────────────
// Smart Order Router
// ─────────────────────────────────────────────────────────────────────────────
class SmartOrderRouter {
public:
    static constexpr size_t MAX_VENUES = 10;
    
    void addVenue(const char* name) {
        if (venue_count_ >= MAX_VENUES) return;
        VenueScore v;
        strncpy(v.venue, name, 31);
        venues_[venue_count_++] = v;
    }
    
    void updateVenue(const char* name, double latency_ms, double fill_rate,
                     double spread_bps, double reject_rate, double commission_bps) {
        for (size_t i = 0; i < venue_count_; i++) {
            if (strcmp(venues_[i].venue, name) == 0) {
                venues_[i].latency_ms = latency_ms;
                venues_[i].fill_rate = fill_rate;
                venues_[i].spread_bps = spread_bps;
                venues_[i].reject_rate = reject_rate;
                venues_[i].commission_bps = commission_bps;
                return;
            }
        }
    }
    
    void setAvailable(const char* name, bool available) {
        for (size_t i = 0; i < venue_count_; i++) {
            if (strcmp(venues_[i].venue, name) == 0) {
                venues_[i].available = available;
                return;
            }
        }
    }
    
    // Mark venues as sharing liquidity provider (for anti-self-trade)
    void setSharedLP(const char* venue1, const char* venue2) {
        for (size_t i = 0; i < lp_pair_count_ && i < MAX_LP_PAIRS; i++) {
            if ((strcmp(lp_pairs_[i].venue1, venue1) == 0 && strcmp(lp_pairs_[i].venue2, venue2) == 0) ||
                (strcmp(lp_pairs_[i].venue1, venue2) == 0 && strcmp(lp_pairs_[i].venue2, venue1) == 0)) {
                return;  // Already registered
            }
        }
        if (lp_pair_count_ < MAX_LP_PAIRS) {
            strncpy(lp_pairs_[lp_pair_count_].venue1, venue1, 31);
            strncpy(lp_pairs_[lp_pair_count_].venue2, venue2, 31);
            lp_pair_count_++;
        }
    }
    
    // Check if routing to this venue risks self-trade
    bool wouldRiskSelfTrade(const char* target_venue) const {
        if (!locked_) return false;
        
        // Same venue? Definitely risk
        if (strcmp(locked_venue_, target_venue) == 0) return true;
        
        // Shared LP? Also risk
        for (size_t i = 0; i < lp_pair_count_; i++) {
            if ((strcmp(lp_pairs_[i].venue1, locked_venue_) == 0 && strcmp(lp_pairs_[i].venue2, target_venue) == 0) ||
                (strcmp(lp_pairs_[i].venue1, target_venue) == 0 && strcmp(lp_pairs_[i].venue2, locked_venue_) == 0)) {
                return true;
            }
        }
        
        return false;
    }
    
    // Lock to single venue for this decision tick
    void lockVenue(const char* venue) {
        strncpy(locked_venue_, venue, 31);
        locked_ = true;
    }
    
    void unlockVenue() {
        locked_ = false;
        locked_venue_[0] = '\0';
    }
    
    bool isLocked() const { return locked_; }
    const char* lockedVenue() const { return locked_venue_; }
    
    RoutingDecision route() const {
        std::vector<VenueScore> available;
        
        for (size_t i = 0; i < venue_count_; i++) {
            // Skip if would risk self-trade
            if (wouldRiskSelfTrade(venues_[i].venue)) continue;
            if (venues_[i].available) {
                available.push_back(venues_[i]);
            }
        }
        
        return chooseVenue(available);
    }
    
    const char* best() const {
        return route().venue;
    }
    
private:
    static constexpr size_t MAX_LP_PAIRS = 10;
    
    struct LPPair {
        char venue1[32] = {0};
        char venue2[32] = {0};
    };
    
    VenueScore venues_[MAX_VENUES];
    size_t venue_count_ = 0;
    
    LPPair lp_pairs_[MAX_LP_PAIRS];
    size_t lp_pair_count_ = 0;
    
    char locked_venue_[32] = {0};
    bool locked_ = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Default Venue Configurations (CFD brokers only)
// ─────────────────────────────────────────────────────────────────────────────
namespace Venues {

inline VenueScore blackbull() {
    VenueScore v;
    strncpy(v.venue, "BLACKBULL", 31);
    v.latency_ms = 5.0;
    v.fill_rate = 0.85;
    v.spread_bps = 2.5;
    v.reject_rate = 0.05;
    v.commission_bps = 3.0;
    return v;
}

inline VenueScore pepperstone() {
    VenueScore v;
    strncpy(v.venue, "PEPPERSTONE", 31);
    v.latency_ms = 4.0;
    v.fill_rate = 0.88;
    v.spread_bps = 2.0;
    v.reject_rate = 0.04;
    v.commission_bps = 2.5;
    return v;
}

inline VenueScore icmarkets() {
    VenueScore v;
    strncpy(v.venue, "ICMARKETS", 31);
    v.latency_ms = 3.5;
    v.fill_rate = 0.90;
    v.spread_bps = 1.8;
    v.reject_rate = 0.03;
    v.commission_bps = 3.5;
    return v;
}

} // namespace Venues

} // namespace Execution
} // namespace Chimera
