// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/risk/BucketQualityGuard.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Prevent trading degraded sessions that never flip negative
// OWNER: Jo
// LAST VERIFIED: 2024-12-25
//
// v7.15: NEW FILE - Session bleed prevention
//
// PRINCIPLE: "Barely positive = capital drag"
// - Compare bucket vs its own history
// - Not just "is it positive?" but "is it degraded?"
// - Auto-disable after 2 consecutive bad sessions
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <array>
#include <iostream>

namespace Chimera {
namespace Risk {

// ─────────────────────────────────────────────────────────────────────────────
// Time Bucket IDs (UTC hours grouped)
// ─────────────────────────────────────────────────────────────────────────────
enum class TimeBucket : uint8_t {
    ASIA_EARLY   = 0,   // 00-04 UTC
    ASIA_LATE    = 1,   // 04-08 UTC  
    LONDON_OPEN  = 2,   // 08-10 UTC
    LONDON_MAIN  = 3,   // 10-12 UTC
    US_OVERLAP   = 4,   // 12-16 UTC (best liquidity)
    US_MAIN      = 5,   // 16-20 UTC
    US_CLOSE     = 6,   // 20-24 UTC
    
    COUNT = 7
};

inline TimeBucket get_bucket(int utc_hour) noexcept {
    if (utc_hour < 4)  return TimeBucket::ASIA_EARLY;
    if (utc_hour < 8)  return TimeBucket::ASIA_LATE;
    if (utc_hour < 10) return TimeBucket::LONDON_OPEN;
    if (utc_hour < 12) return TimeBucket::LONDON_MAIN;
    if (utc_hour < 16) return TimeBucket::US_OVERLAP;
    if (utc_hour < 20) return TimeBucket::US_MAIN;
    return TimeBucket::US_CLOSE;
}

inline const char* bucket_str(TimeBucket b) noexcept {
    switch (b) {
        case TimeBucket::ASIA_EARLY:  return "ASIA_EARLY";
        case TimeBucket::ASIA_LATE:   return "ASIA_LATE";
        case TimeBucket::LONDON_OPEN: return "LONDON_OPEN";
        case TimeBucket::LONDON_MAIN: return "LONDON_MAIN";
        case TimeBucket::US_OVERLAP:  return "US_OVERLAP";
        case TimeBucket::US_MAIN:     return "US_MAIN";
        case TimeBucket::US_CLOSE:    return "US_CLOSE";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Single Bucket Quality Guard
// ─────────────────────────────────────────────────────────────────────────────
struct BucketQualityGuard {
    double baseline = 0.0;         // Historical average expectancy for this bucket
    double current = 0.0;          // Current session's expectancy
    int bad_sessions = 0;          // Consecutive bad sessions
    int updates = 0;
    
    static constexpr double BASELINE_ALPHA = 0.01;  // Slow baseline adaptation
    static constexpr double BAD_THRESHOLD = 0.4;    // Below 40% of baseline = bad
    
    void update(double session_expectancy) noexcept {
        current = session_expectancy;
        updates++;
        
        // Initialize baseline
        if (baseline == 0.0 && session_expectancy != 0.0) {
            baseline = session_expectancy;
            return;
        }
        
        // Slow baseline adaptation
        baseline = (1.0 - BASELINE_ALPHA) * baseline + BASELINE_ALPHA * session_expectancy;
        
        // Check if session is bad relative to baseline
        if (baseline > 0 && session_expectancy < baseline * BAD_THRESHOLD) {
            bad_sessions++;
        } else {
            bad_sessions = 0;
        }
    }
    
    [[nodiscard]] double quality_ratio() const noexcept {
        if (baseline <= 0) return 1.0;
        if (current >= baseline) return 1.0;
        return current / baseline;
    }
    
    [[nodiscard]] double size_multiplier() const noexcept {
        if (updates < 3) return 1.0;  // Need data
        
        // Disable after 2 consecutive bad sessions
        if (bad_sessions >= 2) return 0.0;
        if (bad_sessions == 1) return 0.5;
        
        // Gradual quality-based scaling
        double q = quality_ratio();
        if (q >= 1.0)  return 1.0;
        if (q >= 0.7)  return 0.7;
        if (q >= 0.4)  return 0.4;
        return 0.0;
    }
    
    [[nodiscard]] bool is_disabled() const noexcept {
        return bad_sessions >= 2;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-Symbol Bucket Quality Manager
// ─────────────────────────────────────────────────────────────────────────────
class BucketQualityManager {
public:
    void update(TimeBucket bucket, double session_expectancy) noexcept {
        size_t idx = static_cast<size_t>(bucket);
        if (idx >= static_cast<size_t>(TimeBucket::COUNT)) return;
        
        BucketQualityGuard& guard = buckets_[idx];
        double old_mult = guard.size_multiplier();
        
        guard.update(session_expectancy);
        
        double new_mult = guard.size_multiplier();
        
        // Log changes
        if (old_mult != new_mult) {
            std::cout << "[BUCKET-" << bucket_str(bucket) << "] "
                      << "quality=" << guard.quality_ratio()
                      << " bad_sessions=" << guard.bad_sessions
                      << " mult=" << new_mult << "x"
                      << (guard.is_disabled() ? " DISABLED" : "")
                      << "\n";
        }
    }
    
    [[nodiscard]] double size_multiplier(TimeBucket bucket) const noexcept {
        size_t idx = static_cast<size_t>(bucket);
        if (idx >= static_cast<size_t>(TimeBucket::COUNT)) return 1.0;
        return buckets_[idx].size_multiplier();
    }
    
    [[nodiscard]] double size_multiplier(int utc_hour) const noexcept {
        return size_multiplier(get_bucket(utc_hour));
    }
    
    [[nodiscard]] bool is_disabled(TimeBucket bucket) const noexcept {
        size_t idx = static_cast<size_t>(bucket);
        if (idx >= static_cast<size_t>(TimeBucket::COUNT)) return false;
        return buckets_[idx].is_disabled();
    }
    
    [[nodiscard]] const BucketQualityGuard& get(TimeBucket bucket) const noexcept {
        return buckets_[static_cast<size_t>(bucket)];
    }

private:
    std::array<BucketQualityGuard, static_cast<size_t>(TimeBucket::COUNT)> buckets_{};
};

} // namespace Risk
} // namespace Chimera
