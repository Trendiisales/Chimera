#include "execution/LatencyExecutionGovernor.hpp"
#include "execution/Time.hpp"
#include <algorithm>
#include <vector>
#include <iostream>

LatencyExecutionGovernor::LatencyExecutionGovernor() {
    last_heartbeat_ms_ = monotonic_ms();
    last_regime_change_ms_ = monotonic_ms();
}

void LatencyExecutionGovernor::on_rtt(double rtt_ms, uint64_t now_ms) {
    rtt_samples_.push_back(rtt_ms);
    if (rtt_samples_.size() > MAX_SAMPLES) {
        rtt_samples_.pop_front();
    }
    
    if (rtt_samples_.size() >= 50) {
        evaluate_regime();
    }
}

void LatencyExecutionGovernor::on_loop_heartbeat(uint64_t now_ms) {
    uint64_t gap_ms = safe_age_ms(now_ms, last_heartbeat_ms_);
    
    if (gap_ms > STALL_THRESHOLD_MS) {
        std::cout << "[LATENCY] STALL DETECTED gap=" << gap_ms << "ms\n";
        in_stall_recovery_ = true;
    } else if (in_stall_recovery_ && gap_ms < 500) {
        std::cout << "[LATENCY] STALL RECOVERED\n";
        in_stall_recovery_ = false;
    }
    
    last_heartbeat_ms_ = now_ms;
}

void LatencyExecutionGovernor::evaluate_regime() {
    if (rtt_samples_.empty()) {
        return;
    }
    
    uint64_t now_ms = monotonic_ms();
    uint64_t since_change = safe_age_ms(now_ms, last_regime_change_ms_);
    
    // Hysteresis: prevent regime flapping
    if (since_change < REGIME_HYSTERESIS_MS) {
        return;
    }
    
    std::vector<double> sorted(rtt_samples_.begin(), rtt_samples_.end());
    std::sort(sorted.begin(), sorted.end());
    
    size_t p95_idx = static_cast<size_t>(sorted.size() * 0.95);
    size_t p99_idx = static_cast<size_t>(sorted.size() * 0.99);
    if (p95_idx >= sorted.size()) p95_idx = sorted.size() - 1;
    if (p99_idx >= sorted.size()) p99_idx = sorted.size() - 1;
    
    double p95 = sorted[p95_idx];
    double p99 = sorted[p99_idx];
    double current = rtt_samples_.back();
    
    LatencyRegime new_regime = regime_;
    
    // Regime classification with XAU-tuned thresholds
    if (p99 > HALT_THRESHOLD_MS || current > HALT_THRESHOLD_MS || in_stall_recovery_) {
        new_regime = LatencyRegime::HALT;
    } else if (p95 > DEGRADED_THRESHOLD_MS) {
        new_regime = LatencyRegime::DEGRADED;
    } else if (p95 > NORMAL_THRESHOLD_MS) {
        new_regime = LatencyRegime::NORMAL;
    } else if (p95 <= FAST_THRESHOLD_MS) {
        new_regime = LatencyRegime::FAST;
    }
    
    if (new_regime != regime_) {
        const char* regime_str = 
            new_regime == LatencyRegime::FAST ? "FAST" :
            new_regime == LatencyRegime::NORMAL ? "NORMAL" :
            new_regime == LatencyRegime::DEGRADED ? "DEGRADED" : "HALT";
        
        std::cout << "[LATENCY] REGIME CHANGE: " << regime_str 
                  << " (p95=" << p95 << "ms)\n";
        
        regime_ = new_regime;
        last_regime_change_ms_ = now_ms;
    }
}

LatencySnapshot LatencyExecutionGovernor::snapshot() const {
    if (rtt_samples_.empty()) {
        return {0, 0, 0, 0, regime_};
    }
    
    std::vector<double> sorted(rtt_samples_.begin(), rtt_samples_.end());
    std::sort(sorted.begin(), sorted.end());
    
    auto percentile = [&](double p) {
        size_t idx = static_cast<size_t>(sorted.size() * p);
        if (idx >= sorted.size()) idx = sorted.size() - 1;
        return sorted[idx];
    };
    
    return {
        percentile(0.50),
        percentile(0.90),
        percentile(0.95),
        percentile(0.99),
        regime_
    };
}
