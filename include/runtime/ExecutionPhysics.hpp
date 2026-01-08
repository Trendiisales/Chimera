// ═══════════════════════════════════════════════════════════════════════════════
// include/runtime/ExecutionPhysics.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.11: EXECUTION PHYSICS DETECTOR
//
// PURPOSE: Automatically classify execution environment based on measured latency.
// This is the GROUND TRUTH that drives all capability decisions.
//
// CLASSIFICATION:
// - COLO:      p95 < 1.5ms, jitter < 0.3ms (same datacenter)
// - NEAR_COLO: p95 < 8.0ms, jitter < 2.0ms (same metro/region)
// - WAN:       Everything else (remote VPS)
//
// USAGE:
// - Runs continuously on every latency update
// - No config flags - physics cannot be faked
// - Drives capability matrix automatically
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <vector>

namespace Chimera {
namespace Runtime {

// ─────────────────────────────────────────────────────────────────────────────
// Execution Physics Classification
// ─────────────────────────────────────────────────────────────────────────────
enum class ExecPhysics : uint8_t {
    UNKNOWN    = 0,  // Insufficient data to classify
    WAN        = 1,  // Remote VPS, high latency
    NEAR_COLO  = 2,  // Same metro/region, medium latency
    COLO       = 3   // Same datacenter, sub-ms latency
};

inline const char* execPhysicsStr(ExecPhysics p) {
    switch (p) {
        case ExecPhysics::UNKNOWN:   return "UNKNOWN";
        case ExecPhysics::COLO:      return "COLO";
        case ExecPhysics::NEAR_COLO: return "NEAR_COLO";
        case ExecPhysics::WAN:       return "WAN";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Physics Snapshot - Current execution environment state
// ─────────────────────────────────────────────────────────────────────────────
struct PhysicsSnapshot {
    ExecPhysics physics = ExecPhysics::WAN;
    
    // Latency metrics (ms)
    double ack_p50_ms = 0.0;
    double ack_p95_ms = 0.0;
    double cancel_p95_ms = 0.0;
    double jitter_ms = 0.0;        // p95 - p50
    
    // Stability
    double variance_ms = 0.0;
    bool stable = false;           // Jitter within bounds for physics class
    
    // Sample info
    size_t samples = 0;
    uint64_t last_update_ns = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Physics Detection Thresholds
// ─────────────────────────────────────────────────────────────────────────────
struct PhysicsThresholds {
    // COLO thresholds
    double colo_ack_p95_max_ms = 1.5;
    double colo_jitter_max_ms = 0.3;
    
    // NEAR_COLO thresholds
    double near_colo_ack_p95_max_ms = 8.0;
    double near_colo_jitter_max_ms = 2.0;
    
    // Minimum samples for classification
    size_t min_samples = 10;
};

// ─────────────────────────────────────────────────────────────────────────────
// Classify Physics from Latency Stats
// ─────────────────────────────────────────────────────────────────────────────
inline ExecPhysics classifyPhysics(
    double ack_p95_ms,
    double jitter_ms,
    size_t samples,
    const PhysicsThresholds& thresh = PhysicsThresholds{}
) {
    // CRITICAL: Insufficient samples → UNKNOWN
    // Never classify physics without statistical confidence
    if (samples < thresh.min_samples) {
        return ExecPhysics::UNKNOWN;
    }
    
    // Need at least 200 samples for high-confidence classification
    // Below that, be conservative
    bool high_confidence = samples >= 200;
    
    // COLO: Sub-millisecond with minimal jitter
    if (ack_p95_ms < thresh.colo_ack_p95_max_ms && 
        jitter_ms < thresh.colo_jitter_max_ms) {
        return ExecPhysics::COLO;
    }
    
    // NEAR_COLO: Low latency with acceptable jitter
    if (ack_p95_ms < thresh.near_colo_ack_p95_max_ms && 
        jitter_ms < thresh.near_colo_jitter_max_ms) {
        return high_confidence ? ExecPhysics::NEAR_COLO : ExecPhysics::WAN;
    }
    
    // WAN: Everything else
    return ExecPhysics::WAN;
}

// Legacy overload for backward compatibility
inline ExecPhysics classifyPhysics(
    double ack_p95_ms,
    double jitter_ms,
    const PhysicsThresholds& thresh = PhysicsThresholds{}
) {
    return classifyPhysics(ack_p95_ms, jitter_ms, 200, thresh);  // Assume sufficient
}

// ─────────────────────────────────────────────────────────────────────────────
// Physics Detector - Tracks and classifies execution environment
// ─────────────────────────────────────────────────────────────────────────────
class PhysicsDetector {
public:
    static constexpr size_t MAX_SAMPLES = 1000;
    
    PhysicsDetector() = default;
    
    void recordLatency(double ack_ms, double cancel_ms = 0.0) {
        if (ack_samples_.size() >= MAX_SAMPLES) {
            ack_samples_.erase(ack_samples_.begin());
        }
        ack_samples_.push_back(ack_ms);
        
        if (cancel_ms > 0.0) {
            if (cancel_samples_.size() >= MAX_SAMPLES) {
                cancel_samples_.erase(cancel_samples_.begin());
            }
            cancel_samples_.push_back(cancel_ms);
        }
        
        dirty_ = true;
    }
    
    PhysicsSnapshot detect() {
        if (dirty_) {
            computeStats();
            dirty_ = false;
        }
        return snapshot_;
    }
    
    ExecPhysics currentPhysics() const {
        return snapshot_.physics;
    }
    
    bool hasEnoughSamples() const {
        return ack_samples_.size() >= thresh_.min_samples;
    }
    
    void setThresholds(const PhysicsThresholds& t) {
        thresh_ = t;
        dirty_ = true;
    }
    
private:
    std::vector<double> ack_samples_;
    std::vector<double> cancel_samples_;
    PhysicsSnapshot snapshot_;
    PhysicsThresholds thresh_;
    bool dirty_ = true;
    
    void computeStats() {
        if (ack_samples_.empty()) return;
        
        std::vector<double> sorted = ack_samples_;
        std::sort(sorted.begin(), sorted.end());
        
        size_t n = sorted.size();
        snapshot_.samples = n;
        snapshot_.ack_p50_ms = sorted[n * 50 / 100];
        snapshot_.ack_p95_ms = sorted[std::min(n * 95 / 100, n - 1)];
        snapshot_.jitter_ms = snapshot_.ack_p95_ms - snapshot_.ack_p50_ms;
        
        // Variance
        double sum = 0.0, sum_sq = 0.0;
        for (double v : sorted) {
            sum += v;
            sum_sq += v * v;
        }
        double mean = sum / n;
        snapshot_.variance_ms = std::sqrt(sum_sq / n - mean * mean);
        
        // Cancel latency
        if (!cancel_samples_.empty()) {
            std::vector<double> cancel_sorted = cancel_samples_;
            std::sort(cancel_sorted.begin(), cancel_sorted.end());
            size_t cn = cancel_sorted.size();
            snapshot_.cancel_p95_ms = cancel_sorted[std::min(cn * 95 / 100, cn - 1)];
        }
        
        // Classify with sample count
        snapshot_.physics = classifyPhysics(snapshot_.ack_p95_ms, snapshot_.jitter_ms, n, thresh_);
        
        // Stability check
        switch (snapshot_.physics) {
            case ExecPhysics::COLO:
                snapshot_.stable = snapshot_.jitter_ms < thresh_.colo_jitter_max_ms * 1.5;
                break;
            case ExecPhysics::NEAR_COLO:
                snapshot_.stable = snapshot_.jitter_ms < thresh_.near_colo_jitter_max_ms * 1.5;
                break;
            default:
                snapshot_.stable = snapshot_.variance_ms < 10.0;
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Global Physics Detector Singleton
// ─────────────────────────────────────────────────────────────────────────────
inline PhysicsDetector& getPhysicsDetector() {
    static PhysicsDetector detector;
    return detector;
}

inline PhysicsSnapshot detectPhysics() {
    return getPhysicsDetector().detect();
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-Venue Physics Tracking (prevents one bad venue poisoning global physics)
// ─────────────────────────────────────────────────────────────────────────────
class VenuePhysicsTracker {
public:
    static constexpr size_t MAX_VENUES = 8;
    
    void recordLatency(const char* venue, double ack_ms, double cancel_ms = 0.0) {
        size_t idx = findOrCreate(venue);
        if (idx >= MAX_VENUES) return;
        
        venues_[idx].detector.recordLatency(ack_ms, cancel_ms);
        
        // Spike detection: sudden latency increase
        auto snap = venues_[idx].detector.detect();
        if (snap.samples > 50) {
            double recent_avg = ack_ms;  // Could use EMA
            if (recent_avg > snap.ack_p95_ms * 2.0) {
                venues_[idx].spike_detected = true;
                venues_[idx].spike_ts = std::chrono::steady_clock::now();
            } else if (venues_[idx].spike_detected) {
                // Check if spike has recovered (5 seconds)
                auto elapsed = std::chrono::steady_clock::now() - venues_[idx].spike_ts;
                if (elapsed > std::chrono::seconds(5)) {
                    venues_[idx].spike_detected = false;
                }
            }
        }
    }
    
    PhysicsSnapshot getForVenue(const char* venue) const {
        for (size_t i = 0; i < venue_count_; i++) {
            if (strcmp(venues_[i].name, venue) == 0) {
                return venues_[i].detector.detect();
            }
        }
        return PhysicsSnapshot{};  // Unknown venue
    }
    
    ExecPhysics getPhysicsForVenue(const char* venue) const {
        auto snap = getForVenue(venue);
        return snap.physics;
    }
    
    bool hasSpikeForVenue(const char* venue) const {
        for (size_t i = 0; i < venue_count_; i++) {
            if (strcmp(venues_[i].name, venue) == 0) {
                return venues_[i].spike_detected;
            }
        }
        return false;
    }
    
    // Get worst-case physics across all venues
    ExecPhysics getConservativePhysics() const {
        ExecPhysics worst = ExecPhysics::COLO;
        for (size_t i = 0; i < venue_count_; i++) {
            auto snap = venues_[i].detector.detect();
            if (static_cast<uint8_t>(snap.physics) < static_cast<uint8_t>(worst)) {
                worst = snap.physics;
            }
        }
        return worst;
    }
    
private:
    struct VenueState {
        char name[32] = {0};
        PhysicsDetector detector;
        bool spike_detected = false;
        std::chrono::steady_clock::time_point spike_ts;
    };
    
    VenueState venues_[MAX_VENUES];
    size_t venue_count_ = 0;
    
    size_t findOrCreate(const char* venue) {
        for (size_t i = 0; i < venue_count_; i++) {
            if (strcmp(venues_[i].name, venue) == 0) return i;
        }
        if (venue_count_ < MAX_VENUES) {
            strncpy(venues_[venue_count_].name, venue, 31);
            return venue_count_++;
        }
        return MAX_VENUES;  // Full
    }
};

inline VenuePhysicsTracker& getVenuePhysicsTracker() {
    static VenuePhysicsTracker tracker;
    return tracker;
}

} // namespace Runtime
} // namespace Chimera
