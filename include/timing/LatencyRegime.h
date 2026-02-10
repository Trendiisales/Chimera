#pragma once
#include <cstdint>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>

// ═══════════════════════════════════════════════════════════
// LAYER 2: ROLLING LATENCY BASELINES (Self-updating)
// ═══════════════════════════════════════════════════════════

struct RollingStats {
    double p50 = 0.0;
    double p90 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double mean = 0.0;
    double stddev = 0.0;
    
    void compute(const std::vector<int>& samples) {
        if (samples.empty()) return;
        
        std::vector<int> sorted = samples;
        std::sort(sorted.begin(), sorted.end());
        
        size_t n = sorted.size();
        p50 = sorted[n * 50 / 100];
        p90 = sorted[n * 90 / 100];
        p95 = sorted[n * 95 / 100];
        p99 = sorted[std::min(n * 99 / 100, n - 1)];
        
        double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
        mean = sum / n;
        
        double sq_sum = 0.0;
        for (int x : sorted) {
            sq_sum += (x - mean) * (x - mean);
        }
        stddev = std::sqrt(sq_sum / n);
    }
};

// ═══════════════════════════════════════════════════════════
// LAYER 3: LATENCY REGIME STATES (Adaptive)
// ═══════════════════════════════════════════════════════════

enum class LatencyRegime {
    FAST,       // Historical normal - trade freely
    DEGRADED,   // Worse but usable - reduce risk
    UNSTABLE,   // Heavy tails/jitter - exits only
    UNUSABLE    // Execution unsafe - flatten
};

const char* regimeToString(LatencyRegime r) {
    switch(r) {
        case LatencyRegime::FAST: return "FAST";
        case LatencyRegime::DEGRADED: return "DEGRADED";
        case LatencyRegime::UNSTABLE: return "UNSTABLE";
        case LatencyRegime::UNUSABLE: return "UNUSABLE";
    }
    return "UNKNOWN";
}

// ═══════════════════════════════════════════════════════════
// ROLLING WINDOW MANAGER
// ═══════════════════════════════════════════════════════════

class RollingWindow {
public:
    RollingWindow(size_t max_size, uint64_t window_ms)
        : max_size_(max_size), window_ms_(window_ms) {}
    
    void add(int rtt_ms, uint64_t timestamp_ms) {
        samples_.push_back(rtt_ms);
        timestamps_.push_back(timestamp_ms);
        
        // Remove old samples outside time window
        while (!timestamps_.empty() && 
               timestamp_ms - timestamps_.front() > window_ms_) {
            samples_.erase(samples_.begin());
            timestamps_.erase(timestamps_.begin());
        }
        
        // Limit size
        while (samples_.size() > max_size_) {
            samples_.erase(samples_.begin());
            timestamps_.erase(timestamps_.begin());
        }
    }
    
    const std::vector<int>& samples() const { return samples_; }
    bool ready() const { return samples_.size() >= 10; }
    
private:
    std::vector<int> samples_;
    std::vector<uint64_t> timestamps_;
    size_t max_size_;
    uint64_t window_ms_;
};

// ═══════════════════════════════════════════════════════════
// LATENCY REGIME DETECTOR (Event-driven)
// ═══════════════════════════════════════════════════════════

class LatencyRegimeDetector {
public:
    LatencyRegimeDetector()
        : short_window_(100, 30000)      // 30 seconds - jitter detection
        , medium_window_(300, 900000)    // 15 minutes - micro regime
        , long_window_(1000, 21600000)   // 6 hours - baseline evolution
        , regime_(LatencyRegime::FAST)
        , last_regime_(LatencyRegime::FAST)
        , last_check_ms_(0)
    {}
    
    // Call this on EVERY FIX event (order, heartbeat, quote)
    void onFixRtt(int rtt_ms, uint64_t now_ms) {
        short_window_.add(rtt_ms, now_ms);
        medium_window_.add(rtt_ms, now_ms);
        long_window_.add(rtt_ms, now_ms);
        
        // Update regime every second (not every tick - too expensive)
        if (now_ms - last_check_ms_ > 1000) {
            updateRegime(now_ms);
            last_check_ms_ = now_ms;
        }
    }
    
    LatencyRegime getRegime() const { return regime_; }
    
    const RollingStats& getShortStats() const { return short_stats_; }
    const RollingStats& getMediumStats() const { return medium_stats_; }
    const RollingStats& getLongStats() const { return long_stats_; }
    
    bool regimeChanged() const { return regime_ != last_regime_; }
    
private:
    void updateRegime(uint64_t now_ms) {
        // Compute stats for each window
        if (short_window_.ready()) short_stats_.compute(short_window_.samples());
        if (medium_window_.ready()) medium_stats_.compute(medium_window_.samples());
        if (long_window_.ready()) long_stats_.compute(long_window_.samples());
        
        last_regime_ = regime_;
        
        // Need long baseline to determine if degraded
        if (!long_window_.ready()) {
            regime_ = LatencyRegime::FAST;
            return;
        }
        
        // ═══════════════════════════════════════════════════════════
        // REGIME TRANSITIONS (Adaptive thresholds)
        // ═══════════════════════════════════════════════════════════
        
        double baseline_p95 = long_stats_.p95;
        double baseline_p99 = long_stats_.p99;
        double baseline_stddev = long_stats_.stddev;
        
        double current_p95 = medium_stats_.p95;
        double current_p99 = medium_stats_.p99;
        double current_stddev = medium_stats_.stddev;
        
        // UNUSABLE: Hard cap or extreme degradation
        if (current_p99 > 20.0 || current_p95 > 15.0) {
            regime_ = LatencyRegime::UNUSABLE;
            return;
        }
        
        // UNSTABLE: Heavy tails or stddev spike
        if (current_p99 > baseline_p99 * 1.5 || 
            current_stddev > baseline_stddev * 2.0) {
            regime_ = LatencyRegime::UNSTABLE;
            return;
        }
        
        // DEGRADED: Worse but usable
        if (current_p95 > baseline_p95 * 1.3) {
            regime_ = LatencyRegime::DEGRADED;
            return;
        }
        
        // FAST: Normal operation
        regime_ = LatencyRegime::FAST;
    }
    
    RollingWindow short_window_;   // 30s - jitter
    RollingWindow medium_window_;  // 15min - micro regime
    RollingWindow long_window_;    // 6hr - baseline
    
    RollingStats short_stats_;
    RollingStats medium_stats_;
    RollingStats long_stats_;
    
    LatencyRegime regime_;
    LatencyRegime last_regime_;
    uint64_t last_check_ms_;
};

