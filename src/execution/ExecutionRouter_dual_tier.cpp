#include "execution/ExecutionRouter.hpp"
#include "execution/Time.hpp"
#include "execution/XAUImpulseGate.h"
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
    
    // Get current latency regime and snapshot
    LatencyRegime regime = latency_.regime();
    auto snap = latency_.snapshot();
    
    // DEGRADED/HALT: no XAU trading
    if (regime == LatencyRegime::DEGRADED || regime == LatencyRegime::HALT) {
        reject_reason = "XAU_LATENCY_NOT_GOOD";
        return false;
    }
    
    // Get current velocity
    double vel = xau_velocity_.ema_velocity();
    double abs_vel = std::abs(vel);
    
    // === DUAL-TIER IMPULSE GATING ===
    // Build LatencyStats for gate evaluation
    LatencyStats lat_stats;
    lat_stats.p50 = snap.p50_ms;
    lat_stats.p90 = snap.p90_ms;
    lat_stats.p95 = snap.p95_ms;
    lat_stats.p99 = snap.p99_ms;
    
    // Note: We don't have spread or legs here, so use conservative values
    // This will be fully integrated when we add spread tracking
    double spread = 0.25;  // Typical XAU spread
    int legs = 0;          // Assume no legs (will be passed from SymbolExecutor)
    
    XAUImpulseDecision decision = XAUImpulseGate::evaluate(
        vel,
        spread,
        legs,
        lat_stats
    );
    
    if (!decision.allowed) {
        reject_reason = "XAU_NO_IMPULSE";
        std::cout << "[XAUUSD] REJECT: " << reject_reason 
                  << " (vel=" << vel << ", abs=" << abs_vel << ")\n";
        return false;
    }
    
    // Log entry type
    if (decision.soft) {
        std::cout << "[XAUUSD] ✓ SOFT IMPULSE ENTRY (vel=" << vel 
                  << ", abs=" << abs_vel 
                  << ", lat_fast=" << lat_stats.is_fast() << ")\n";
    } else {
        std::cout << "[XAUUSD] ✓ HARD IMPULSE ENTRY (vel=" << vel 
                  << ", abs=" << abs_vel << ")\n";
    }
    
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

double ExecutionRouter::get_velocity(const std::string& symbol) const {
    if (symbol == "XAUUSD") {
        return xau_velocity_.ema_velocity();
    } else if (symbol == "XAGUSD") {
        return xag_velocity_.ema_velocity();
    }
    return 0.0;
}
