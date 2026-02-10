#include "execution/ExecutionRouter.hpp"
#include "execution/Time.hpp"
#include <iostream>
#include <cmath>

ExecutionRouter::ExecutionRouter()
    : latency_()
    , xau_velocity_()
    , xag_velocity_()
{
}

void ExecutionRouter::on_quote(const std::string& symbol, const Quote& q) {
    double mid = (q.bid + q.ask) * 0.5;
    
    if (symbol == "XAUUSD") {
        xau_velocity_.record(mid, q.ts_ms);
    } else if (symbol == "XAGUSD") {
        xag_velocity_.record(mid, q.ts_ms);
    }
}

void ExecutionRouter::on_fix_rtt(double rtt_ms, uint64_t now_ms) {
    latency_.on_rtt(rtt_ms, now_ms);
}

void ExecutionRouter::on_loop_heartbeat(uint64_t now_ms) {
    latency_.on_loop_heartbeat(now_ms);
}

bool ExecutionRouter::submit_signal(
    const std::string& symbol,
    bool is_buy,
    uint64_t signal_ts_ms,
    std::string& reject_reason
) {
    uint64_t now_ms = monotonic_ms();
    
    if (symbol == "XAUUSD") {
        return submit_xau(signal_ts_ms, now_ms, reject_reason);
    } else if (symbol == "XAGUSD") {
        return submit_xag(signal_ts_ms, now_ms, reject_reason);
    }
    
    reject_reason = "UNKNOWN_SYMBOL";
    return false;
}

bool ExecutionRouter::submit_xau(
    uint64_t signal_ts_ms,
    uint64_t now_ms,
    std::string& reject_reason
) {
    // Signal age with tolerance for micro-batching
    int64_t age_ms = static_cast<int64_t>(now_ms) - static_cast<int64_t>(signal_ts_ms);
    
    // Allow -3ms to +120ms (tolerates minor clock skew and batching)
    if (age_ms < -3) {
        reject_reason = "XAU_SIGNAL_FROM_FUTURE";
        std::cout << "[XAUUSD] REJECT: " << reject_reason 
                  << " (age=" << age_ms << "ms)\n";
        return false;
    }
    
    if (age_ms > 120) {
        reject_reason = "XAU_SIGNAL_STALE";
        std::cout << "[XAUUSD] REJECT: " << reject_reason 
                  << " (age=" << age_ms << "ms)\n";
        return false;
    }
    
    // Get current latency regime
    LatencyRegime regime = latency_.regime();
    auto snap = latency_.snapshot();
    
    // Adaptive thresholds based on latency regime
    double impulse_threshold;
    if (regime == LatencyRegime::FAST && snap.p95_ms < 5.0) {
        // ULTRA_FAST mode: aggressive threshold
        impulse_threshold = 0.18;
    } else if (regime == LatencyRegime::FAST) {
        // FAST mode: moderate threshold
        impulse_threshold = 0.25;
    } else if (regime == LatencyRegime::NORMAL) {
        // NORMAL mode: relaxed threshold
        impulse_threshold = 0.08;
    } else {
        // DEGRADED/HALT: no XAU trading
        reject_reason = "XAU_LATENCY_NOT_GOOD";
        return false;
    }
    
    // Check EMA velocity against adaptive threshold
    double vel = xau_velocity_.ema_velocity();
    if (std::abs(vel) < impulse_threshold) {
        reject_reason = "XAU_NO_IMPULSE";
        std::cout << "[XAUUSD] REJECT: " << reject_reason 
                  << " (vel=" << vel << ", threshold=" << impulse_threshold << ")\n";
        return false;
    }
    
    std::cout << "[XAUUSD] ✓ ENTRY ALLOWED (regime=" 
              << (regime == LatencyRegime::FAST ? "FAST" : "NORMAL")
              << ", vel=" << vel << ", threshold=" << impulse_threshold << ")\n";
    
    return true;
}

bool ExecutionRouter::submit_xag(
    uint64_t signal_ts_ms,
    uint64_t now_ms,
    std::string& reject_reason
) {
    // XAG is more tolerant - only block in DEGRADED/HALT
    LatencyRegime regime = latency_.regime();
    if (regime == LatencyRegime::DEGRADED || regime == LatencyRegime::HALT) {
        reject_reason = "XAG_LATENCY_DEGRADED";
        return false;
    }
    
    return true;
}

void ExecutionRouter::dump_status() const {
    auto snap = latency_.snapshot();
    const char* regime_str = 
        snap.regime == LatencyRegime::FAST ? "FAST" :
        snap.regime == LatencyRegime::NORMAL ? "NORMAL" :
        snap.regime == LatencyRegime::DEGRADED ? "DEGRADED" : "HALT";
    
    std::cout << "[LATENCY] regime=" << regime_str
              << " p50=" << snap.p50_ms
              << " p90=" << snap.p90_ms
              << " p95=" << snap.p95_ms
              << " p99=" << snap.p99_ms << "\n";
    
    double xau_vel = xau_velocity_.ema_velocity();
    double xag_vel = xag_velocity_.ema_velocity();
    std::cout << "[VELOCITY] XAU=" << xau_vel << " XAG=" << xag_vel << "\n";
}

const LatencyExecutionGovernor& ExecutionRouter::latency() const {
    return latency_;
}
