#pragma once
// =============================================================================
// CrossVenueArbGuard.hpp v4.2.2 - Cross-Venue Arbitrage Protection
// =============================================================================
// Prevents self-arbitrage losses when price discovery across venues diverges.
// Critical for multi-venue systems where latency asymmetry exists.
//
// Triggers:
//   - Price dislocation > threshold
//   - Latency asymmetry detected
//   - Book desync across venues
//
// Actions:
//   - Freeze one venue
//   - Block new entries
//   - Force time-aligned tick comparison
// =============================================================================

#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <array>
#include <iostream>
#include <iomanip>

namespace Omega {

// =============================================================================
// VENUE PRICE SNAPSHOT - Point-in-time price from a single venue
// =============================================================================
struct VenuePriceSnapshot {
    double bid = 0.0;
    double ask = 0.0;
    uint64_t timestamp_ns = 0;
    bool valid = false;
    
    double mid() const { return (bid + ask) * 0.5; }
    double spread() const { return ask - bid; }
    double spreadBps() const { 
        double m = mid();
        return m > 0 ? (spread() / m) * 10000.0 : 0.0;
    }
};

// =============================================================================
// ARB GUARD DECISION
// =============================================================================
struct ArbGuardDecision {
    bool allow_trade = true;
    double price_dislocation_bps = 0.0;
    uint64_t latency_diff_ns = 0;
    const char* block_reason = "";
};

// =============================================================================
// ARB GUARD CONFIG
// =============================================================================
struct ArbGuardConfig {
    double max_dislocation_bps = 3.0;      // Max price difference before blocking
    uint64_t max_timestamp_diff_ns = 50'000'000;  // 50ms max staleness
    double spread_explosion_mult = 3.0;     // Block if spread > 3× normal
    bool enabled = true;
};

// =============================================================================
// CROSS-VENUE ARB GUARD - Per-symbol protection
// =============================================================================
class CrossVenueArbGuard {
public:
    static constexpr size_t MAX_VENUES = 4;
    
    struct VenueState {
        VenuePriceSnapshot last_snap;
        double normal_spread_bps = 1.0;
        bool frozen = false;
        uint64_t frozen_until_ns = 0;
        const char* venue_name = "";
    };
    
private:
    std::array<VenueState, MAX_VENUES> venues_;
    size_t venue_count_ = 0;
    ArbGuardConfig config_;
    const char* symbol_ = "";
    
    // Stats
    uint64_t blocks_dislocation_ = 0;
    uint64_t blocks_staleness_ = 0;
    uint64_t blocks_spread_ = 0;
    
public:
    void init(const char* symbol, const ArbGuardConfig& cfg = {}) {
        symbol_ = symbol;
        config_ = cfg;
        venue_count_ = 0;
    }
    
    size_t registerVenue(const char* venue_name, double normal_spread_bps = 1.0) {
        if (venue_count_ >= MAX_VENUES) return MAX_VENUES;
        venues_[venue_count_].venue_name = venue_name;
        venues_[venue_count_].normal_spread_bps = normal_spread_bps;
        return venue_count_++;
    }
    
    void updateVenue(size_t venue_idx, const VenuePriceSnapshot& snap) {
        if (venue_idx >= venue_count_) return;
        venues_[venue_idx].last_snap = snap;
    }
    
    // Check if trading is safe given current venue state
    ArbGuardDecision evaluate([[maybe_unused]] uint64_t now_ns) {
        if (!config_.enabled || venue_count_ < 2) {
            return { true, 0.0, 0, "" };
        }
        
        // Find freshest venue
        size_t freshest_idx = 0;
        uint64_t freshest_ts = 0;
        for (size_t i = 0; i < venue_count_; i++) {
            if (venues_[i].last_snap.valid && 
                venues_[i].last_snap.timestamp_ns > freshest_ts) {
                freshest_ts = venues_[i].last_snap.timestamp_ns;
                freshest_idx = i;
            }
        }
        
        if (freshest_ts == 0) {
            return { false, 0.0, 0, "NO_VALID_PRICES" };
        }
        
        const auto& fresh = venues_[freshest_idx];
        
        // Check all other venues for dislocation
        for (size_t i = 0; i < venue_count_; i++) {
            if (i == freshest_idx) continue;
            if (!venues_[i].last_snap.valid) continue;
            
            const auto& other = venues_[i];
            
            // Check timestamp staleness
            uint64_t ts_diff = (fresh.last_snap.timestamp_ns > other.last_snap.timestamp_ns) ?
                               (fresh.last_snap.timestamp_ns - other.last_snap.timestamp_ns) :
                               (other.last_snap.timestamp_ns - fresh.last_snap.timestamp_ns);
            
            if (ts_diff > config_.max_timestamp_diff_ns) {
                blocks_staleness_++;
                return { false, 0.0, ts_diff, "VENUE_STALE" };
            }
            
            // Check price dislocation
            double mid_fresh = fresh.last_snap.mid();
            double mid_other = other.last_snap.mid();
            double dislocation = std::abs(mid_fresh - mid_other);
            double dislocation_bps = (mid_fresh > 0) ? (dislocation / mid_fresh) * 10000.0 : 0.0;
            
            if (dislocation_bps > config_.max_dislocation_bps) {
                blocks_dislocation_++;
                std::cout << "[ARB-GUARD " << symbol_ << "] BLOCKED: dislocation=" 
                          << std::fixed << std::setprecision(2) << dislocation_bps 
                          << "bps between " << fresh.venue_name << " and " << other.venue_name << "\n";
                return { false, dislocation_bps, ts_diff, "PRICE_DISLOCATION" };
            }
            
            // Check spread explosion
            double spread_bps = other.last_snap.spreadBps();
            if (spread_bps > other.normal_spread_bps * config_.spread_explosion_mult) {
                blocks_spread_++;
                return { false, dislocation_bps, ts_diff, "SPREAD_EXPLOSION" };
            }
        }
        
        return { true, 0.0, 0, "" };
    }
    
    // Freeze a venue temporarily
    void freezeVenue(size_t venue_idx, uint64_t duration_ns, uint64_t now_ns) {
        if (venue_idx >= venue_count_) return;
        venues_[venue_idx].frozen = true;
        venues_[venue_idx].frozen_until_ns = now_ns + duration_ns;
        std::cout << "[ARB-GUARD " << symbol_ << "] Venue " 
                  << venues_[venue_idx].venue_name << " FROZEN for " 
                  << (duration_ns / 1'000'000) << "ms\n";
    }
    
    void unfreezeExpired(uint64_t now_ns) {
        for (size_t i = 0; i < venue_count_; i++) {
            if (venues_[i].frozen && now_ns >= venues_[i].frozen_until_ns) {
                venues_[i].frozen = false;
                std::cout << "[ARB-GUARD " << symbol_ << "] Venue " 
                          << venues_[i].venue_name << " UNFROZEN\n";
            }
        }
    }
    
    bool isVenueFrozen(size_t venue_idx) const {
        return venue_idx < venue_count_ && venues_[venue_idx].frozen;
    }
    
    // Stats
    uint64_t totalBlocks() const { 
        return blocks_dislocation_ + blocks_staleness_ + blocks_spread_; 
    }
    
    void logStats() const {
        std::cout << "[ARB-GUARD " << symbol_ << "] blocks: dislocation=" 
                  << blocks_dislocation_ << " staleness=" << blocks_staleness_
                  << " spread=" << blocks_spread_ << "\n";
    }
};

// =============================================================================
// GLOBAL ARB GUARD MANAGER - For multi-symbol systems
// =============================================================================
class ArbGuardManager {
public:
    static constexpr size_t MAX_SYMBOLS = 30;
    
private:
    std::array<CrossVenueArbGuard, MAX_SYMBOLS> guards_;
    std::array<const char*, MAX_SYMBOLS> symbols_;
    size_t count_ = 0;
    
public:
    CrossVenueArbGuard* getOrCreate(const char* symbol) {
        // Find existing
        for (size_t i = 0; i < count_; i++) {
            if (strcmp(symbols_[i], symbol) == 0) {
                return &guards_[i];
            }
        }
        
        // Create new
        if (count_ >= MAX_SYMBOLS) return nullptr;
        symbols_[count_] = symbol;
        guards_[count_].init(symbol);
        return &guards_[count_++];
    }
    
    void evaluateAll(uint64_t now_ns) {
        for (size_t i = 0; i < count_; i++) {
            guards_[i].unfreezeExpired(now_ns);
        }
    }
    
    void logAllStats() const {
        for (size_t i = 0; i < count_; i++) {
            guards_[i].logStats();
        }
    }
};

} // namespace Omega
