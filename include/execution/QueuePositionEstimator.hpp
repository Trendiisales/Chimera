// ═══════════════════════════════════════════════════════════════════════════════
// include/execution/QueuePositionEstimator.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.11: QUEUE POSITION ESTIMATION (COLO-ONLY)
//
// PURPOSE: Estimate queue position and fill probability.
// Only meaningful in COLO environments where we have speed to compete.
//
// DISABLED AUTOMATICALLY when ExecCapabilities.allow_queue_estimation == false
//
// INPUTS:
// - Depth ahead of our order
// - Recent trade rate
// - Our ACK latency
//
// OUTPUT:
// - Estimated quantity ahead
// - Fill probability
// - Expected wait time
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace Chimera {
namespace Execution {

// ─────────────────────────────────────────────────────────────────────────────
// Queue Estimate
// ─────────────────────────────────────────────────────────────────────────────
struct QueueEstimate {
    double qty_ahead = 0.0;           // Estimated quantity ahead in queue
    double fill_probability = 0.0;    // Probability of fill (0-1)
    double expected_wait_ms = 0.0;    // Expected time to fill
    double time_at_front_ms = 0.0;    // Time remaining once at front
    bool valid = false;               // Is this estimate meaningful?
    
    // Confidence band (new)
    double confidence = 0.0;          // 0-1, statistical confidence
    double prob_lower = 0.0;          // Lower bound of fill probability
    double prob_upper = 0.0;          // Upper bound of fill probability
};

// ─────────────────────────────────────────────────────────────────────────────
// Confidence Thresholds
// ─────────────────────────────────────────────────────────────────────────────
struct QueueConfidenceConfig {
    double min_confidence_to_use = 0.6;     // Don't use estimate below this
    double high_confidence = 0.85;          // High confidence threshold
    size_t min_samples_for_confidence = 50; // Need this many samples
};

// ─────────────────────────────────────────────────────────────────────────────
// Queue Estimator Configuration
// ─────────────────────────────────────────────────────────────────────────────
struct QueueEstimatorConfig {
    // Latency thresholds for fill probability
    double fast_ack_ms = 1.0;         // Fast enough for high probability
    double slow_ack_ms = 5.0;         // Too slow for reliable estimation
    
    // Fill probability adjustments
    double base_fill_prob = 0.60;     // Base probability at queue front
    double latency_penalty = 0.10;    // Probability reduction per ms latency
    
    // Queue decay
    double queue_decay_rate = 0.95;   // Queue shrinks this fast per second
};

// ─────────────────────────────────────────────────────────────────────────────
// Estimate Queue Position
// ─────────────────────────────────────────────────────────────────────────────
inline QueueEstimate estimateQueue(
    double depth_ahead,          // Quantity ahead of us in book
    double recent_trade_rate,    // Trades per second at this level
    double ack_ms,               // Our ACK latency
    size_t latency_samples = 100,// Number of latency samples we have
    bool colo_only = true,       // Only valid in COLO?
    const QueueEstimatorConfig& cfg = QueueEstimatorConfig{},
    const QueueConfidenceConfig& conf_cfg = QueueConfidenceConfig{}
) {
    QueueEstimate est;
    
    // Invalid if too slow
    if (colo_only && ack_ms > cfg.slow_ack_ms) {
        est.valid = false;
        est.fill_probability = 0.0;
        est.confidence = 0.0;
        return est;
    }
    
    // Compute confidence based on sample count
    if (latency_samples < conf_cfg.min_samples_for_confidence) {
        est.confidence = static_cast<double>(latency_samples) / conf_cfg.min_samples_for_confidence;
    } else if (latency_samples >= 200) {
        est.confidence = 0.95;
    } else {
        est.confidence = 0.6 + 0.35 * (latency_samples - 50) / 150.0;
    }
    
    // Below minimum confidence? Mark as invalid
    if (est.confidence < conf_cfg.min_confidence_to_use) {
        est.valid = false;
        return est;
    }
    
    est.valid = true;
    est.qty_ahead = depth_ahead;
    
    // Expected wait time
    if (recent_trade_rate > 0) {
        est.expected_wait_ms = (depth_ahead / recent_trade_rate) * 1000.0;
    } else {
        est.expected_wait_ms = 10000.0;  // Very long if no trades
    }
    
    // Fill probability based on latency
    if (ack_ms < cfg.fast_ack_ms) {
        est.fill_probability = cfg.base_fill_prob + 0.15;  // Bonus for speed
    } else {
        double penalty = (ack_ms - cfg.fast_ack_ms) * cfg.latency_penalty;
        est.fill_probability = std::max(0.1, cfg.base_fill_prob - penalty);
    }
    
    // Adjust for queue depth
    if (depth_ahead > 100.0) {
        est.fill_probability *= 0.8;  // Deep queue reduces probability
    } else if (depth_ahead < 10.0) {
        est.fill_probability = std::min(0.95, est.fill_probability * 1.2);
    }
    
    // Compute confidence band (prob_lower, prob_upper)
    double uncertainty = (1.0 - est.confidence) * 0.3;
    est.prob_lower = std::max(0.0, est.fill_probability - uncertainty);
    est.prob_upper = std::min(1.0, est.fill_probability + uncertainty);
    
    // Time at front estimate (how long we'll be front of queue)
    est.time_at_front_ms = 1000.0 / std::max(0.1, recent_trade_rate);
    
    return est;
}

// Legacy overload for backward compatibility
inline QueueEstimate estimateQueue(
    double depth_ahead,
    double recent_trade_rate,
    double ack_ms,
    bool colo_only,
    const QueueEstimatorConfig& cfg = QueueEstimatorConfig{}
) {
    return estimateQueue(depth_ahead, recent_trade_rate, ack_ms, 100, colo_only, cfg);
}

// ─────────────────────────────────────────────────────────────────────────────
// Should We Wait or Cancel?
// ─────────────────────────────────────────────────────────────────────────────
struct WaitDecision {
    bool should_wait = false;
    bool should_cancel = false;
    bool should_repost = false;
    const char* reason = "UNKNOWN";
};

inline WaitDecision shouldWait(
    const QueueEstimate& est,
    double elapsed_ms,           // Time since order placed
    double maker_timeout_ms,     // Max time to wait
    double edge_decay_per_ms     // How fast edge decays
) {
    WaitDecision dec;
    
    if (!est.valid) {
        dec.should_cancel = true;
        dec.reason = "INVALID_ESTIMATE";
        return dec;
    }
    
    // Timeout check
    if (elapsed_ms > maker_timeout_ms) {
        dec.should_cancel = true;
        dec.reason = "TIMEOUT";
        return dec;
    }
    
    // Fill probability check
    if (est.fill_probability < 0.15) {
        dec.should_cancel = true;
        dec.reason = "LOW_FILL_PROB";
        return dec;
    }
    
    // Edge decay check
    double remaining_edge = 1.0 - (elapsed_ms * edge_decay_per_ms / 100.0);
    if (remaining_edge < 0.3) {
        dec.should_cancel = true;
        dec.reason = "EDGE_DECAYED";
        return dec;
    }
    
    // Queue too deep and we've waited long enough
    if (est.qty_ahead > 50.0 && elapsed_ms > maker_timeout_ms * 0.5) {
        dec.should_repost = true;
        dec.reason = "QUEUE_TOO_DEEP";
        return dec;
    }
    
    // Default: keep waiting
    dec.should_wait = true;
    dec.reason = "WAITING";
    return dec;
}

// ─────────────────────────────────────────────────────────────────────────────
// Queue Monitor (tracks queue state over time)
// ─────────────────────────────────────────────────────────────────────────────
class QueueMonitor {
public:
    void update(double depth, double trade_rate, uint64_t now_ns) {
        last_depth_ = depth;
        last_trade_rate_ = trade_rate;
        last_update_ns_ = now_ns;
        
        // Track trade rate history
        if (trade_rates_.size() >= 100) {
            trade_rates_.erase(trade_rates_.begin());
        }
        trade_rates_.push_back(trade_rate);
    }
    
    double avgTradeRate() const {
        if (trade_rates_.empty()) return 0.0;
        double sum = 0.0;
        for (double r : trade_rates_) sum += r;
        return sum / trade_rates_.size();
    }
    
    QueueEstimate estimate(double ack_ms) const {
        return estimateQueue(last_depth_, avgTradeRate(), ack_ms, true);
    }
    
private:
    double last_depth_ = 0.0;
    double last_trade_rate_ = 0.0;
    uint64_t last_update_ns_ = 0;
    std::vector<double> trade_rates_;
};

} // namespace Execution
} // namespace Chimera
