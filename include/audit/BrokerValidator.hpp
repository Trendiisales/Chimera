// ═══════════════════════════════════════════════════════════════════════════════
// include/audit/BrokerValidator.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.11: BROKER END-TO-END VALIDATION HARNESS
//
// PURPOSE: Score brokers empirically, not by marketing claims.
// Run identical probes on different brokers and compare objectively.
//
// METRICS:
// - ACK latency (p50/p80/p95)
// - Cancel latency (p95)
// - Reject rate
// - Fill rate (maker vs taker)
// - Effective spread (bps)
// - Slippage (bps)
//
// SCORING:
// - Lower latency = better
// - Lower cost = better
// - Lower rejects = better
// - Higher maker fills = better
//
// OUTPUT:
// - Per-broker scorecard
// - Comparison report
// - Recommendation
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <mutex>
#include <fstream>
#include <algorithm>
#include <cmath>

namespace Chimera {
namespace Audit {

// ─────────────────────────────────────────────────────────────────────────────
// Broker Score - Composite metric
// ─────────────────────────────────────────────────────────────────────────────
struct BrokerScore {
    char broker[32] = {0};
    char symbol[16] = {0};
    
    // Latency metrics (ms)
    double ack_p50_ms = 0.0;
    double ack_p80_ms = 0.0;
    double ack_p95_ms = 0.0;
    double cancel_p95_ms = 0.0;
    
    // Execution metrics
    double reject_rate = 0.0;        // 0-1
    double maker_fill_rate = 0.0;    // 0-1
    double taker_fill_rate = 0.0;    // 0-1
    
    // Cost metrics (bps)
    double taker_cost_bps = 0.0;     // fee + slippage
    double maker_cost_bps = 0.0;     // fee - rebate
    double spread_bps = 0.0;         // average spread
    double slippage_bps = 0.0;       // average slippage
    
    // Sample counts
    uint64_t samples = 0;
    uint64_t orders_tested = 0;
    
    // Timestamp
    uint64_t last_updated_ns = 0;
    
    // Compute composite score (higher = better)
    double score() const {
        // Weights (negative for "lower is better" metrics)
        return
            -0.40 * ack_p95_ms          // Latency penalty
            -0.20 * cancel_p95_ms       // Cancel latency penalty
            -50.0 * reject_rate         // Reject penalty (heavy)
            -1.00 * taker_cost_bps      // Taker cost penalty
            -0.50 * spread_bps          // Spread penalty
            -2.00 * slippage_bps        // Slippage penalty (heavy)
            +20.0 * maker_fill_rate     // Maker fill bonus
            +10.0 * taker_fill_rate;    // Taker fill bonus
    }
    
    // Letter grade
    const char* grade() const {
        double s = score();
        if (s > 10.0) return "A+";
        if (s > 5.0) return "A";
        if (s > 0.0) return "B";
        if (s > -5.0) return "C";
        if (s > -15.0) return "D";
        return "F";
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Latency Sample for broker validation
// ─────────────────────────────────────────────────────────────────────────────
struct LatencySample {
    double ack_ms = 0.0;
    double cancel_ms = 0.0;
    bool rejected = false;
    bool filled = false;
    bool maker = false;
    double slippage_bps = 0.0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Broker Validator - Collects and scores broker performance
// ─────────────────────────────────────────────────────────────────────────────
class BrokerValidator {
public:
    static constexpr size_t MAX_SAMPLES = 10000;
    
    BrokerValidator(const char* broker, const char* symbol) {
        strncpy(broker_, broker, 31);
        strncpy(symbol_, symbol, 15);
    }
    
    void recordSample(const LatencySample& sample) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (samples_.size() >= MAX_SAMPLES) {
            samples_.erase(samples_.begin());
        }
        samples_.push_back(sample);
    }
    
    void setSpread(double spread_bps) {
        std::lock_guard<std::mutex> lock(mutex_);
        spread_sum_ += spread_bps;
        spread_count_++;
    }
    
    void setFees(double taker_fee_bps, double maker_fee_bps) {
        taker_fee_bps_ = taker_fee_bps;
        maker_fee_bps_ = maker_fee_bps;
    }
    
    BrokerScore computeScore() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        BrokerScore score;
        strncpy(score.broker, broker_, 31);
        strncpy(score.symbol, symbol_, 15);
        score.samples = samples_.size();
        score.orders_tested = samples_.size();
        
        if (samples_.empty()) return score;
        
        // Collect latencies
        std::vector<double> ack_times, cancel_times, slippages;
        size_t rejects = 0, fills = 0, maker_fills = 0, taker_fills = 0;
        
        for (const auto& s : samples_) {
            if (!s.rejected) {
                ack_times.push_back(s.ack_ms);
                if (s.cancel_ms > 0) cancel_times.push_back(s.cancel_ms);
                if (s.filled) {
                    fills++;
                    slippages.push_back(s.slippage_bps);
                    if (s.maker) maker_fills++;
                    else taker_fills++;
                }
            } else {
                rejects++;
            }
        }
        
        // Compute percentiles
        if (!ack_times.empty()) {
            std::sort(ack_times.begin(), ack_times.end());
            size_t n = ack_times.size();
            score.ack_p50_ms = ack_times[n * 50 / 100];
            score.ack_p80_ms = ack_times[n * 80 / 100];
            score.ack_p95_ms = ack_times[std::min(n * 95 / 100, n - 1)];
        }
        
        if (!cancel_times.empty()) {
            std::sort(cancel_times.begin(), cancel_times.end());
            size_t n = cancel_times.size();
            score.cancel_p95_ms = cancel_times[std::min(n * 95 / 100, n - 1)];
        }
        
        // Rates
        score.reject_rate = static_cast<double>(rejects) / samples_.size();
        size_t non_rejects = samples_.size() - rejects;
        if (non_rejects > 0) {
            score.maker_fill_rate = static_cast<double>(maker_fills) / non_rejects;
            score.taker_fill_rate = static_cast<double>(taker_fills) / non_rejects;
        }
        
        // Slippage
        if (!slippages.empty()) {
            double sum = 0.0;
            for (double s : slippages) sum += s;
            score.slippage_bps = sum / slippages.size();
        }
        
        // Spread
        if (spread_count_ > 0) {
            score.spread_bps = spread_sum_ / spread_count_;
        }
        
        // Cost
        score.taker_cost_bps = taker_fee_bps_ + score.slippage_bps;
        score.maker_cost_bps = maker_fee_bps_;
        
        return score;
    }
    
    void exportLog(const char* dir = "runtime/audit/") const {
        BrokerScore s = computeScore();
        
        std::string path = std::string(dir) + "broker_" + broker_ + "_" + symbol_ + ".log";
        std::ofstream f(path);
        if (!f.is_open()) return;
        
        f << "# Broker Validation Report\n";
        f << "broker=" << s.broker << "\n";
        f << "symbol=" << s.symbol << "\n";
        f << "samples=" << s.samples << "\n";
        f << "\n# Latency (ms)\n";
        f << "ack_p50_ms=" << s.ack_p50_ms << "\n";
        f << "ack_p80_ms=" << s.ack_p80_ms << "\n";
        f << "ack_p95_ms=" << s.ack_p95_ms << "\n";
        f << "cancel_p95_ms=" << s.cancel_p95_ms << "\n";
        f << "\n# Execution\n";
        f << "reject_rate=" << s.reject_rate << "\n";
        f << "maker_fill_rate=" << s.maker_fill_rate << "\n";
        f << "taker_fill_rate=" << s.taker_fill_rate << "\n";
        f << "\n# Cost (bps)\n";
        f << "taker_cost_bps=" << s.taker_cost_bps << "\n";
        f << "maker_cost_bps=" << s.maker_cost_bps << "\n";
        f << "spread_bps=" << s.spread_bps << "\n";
        f << "slippage_bps=" << s.slippage_bps << "\n";
        f << "\n# Score\n";
        f << "score=" << s.score() << "\n";
        f << "grade=" << s.grade() << "\n";
        
        f.close();
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        samples_.clear();
        spread_sum_ = 0.0;
        spread_count_ = 0;
    }
    
private:
    char broker_[32] = {0};
    char symbol_[16] = {0};
    mutable std::mutex mutex_;
    std::vector<LatencySample> samples_;
    double spread_sum_ = 0.0;
    size_t spread_count_ = 0;
    double taker_fee_bps_ = 5.0;
    double maker_fee_bps_ = 2.0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Broker Comparison - Compare multiple brokers
// ─────────────────────────────────────────────────────────────────────────────
struct BrokerComparison {
    std::vector<BrokerScore> scores;
    
    void add(const BrokerScore& score) {
        scores.push_back(score);
    }
    
    // Sort by score (best first)
    void sort() {
        std::sort(scores.begin(), scores.end(), 
                  [](const BrokerScore& a, const BrokerScore& b) {
                      return a.score() > b.score();
                  });
    }
    
    // Get recommendation
    const char* getBestBroker() const {
        if (scores.empty()) return "NONE";
        
        double best_score = scores[0].score();
        size_t best_idx = 0;
        
        for (size_t i = 1; i < scores.size(); i++) {
            if (scores[i].score() > best_score) {
                best_score = scores[i].score();
                best_idx = i;
            }
        }
        
        return scores[best_idx].broker;
    }
    
    // Export comparison report
    void exportReport(const char* path = "runtime/audit/broker_comparison.csv") const {
        std::ofstream f(path);
        if (!f.is_open()) return;
        
        // Header
        f << "broker,symbol,ack_p95_ms,cancel_p95_ms,reject_rate,"
          << "maker_fill_rate,taker_cost_bps,spread_bps,slippage_bps,"
          << "score,grade\n";
        
        // Data
        for (const auto& s : scores) {
            f << s.broker << ","
              << s.symbol << ","
              << s.ack_p95_ms << ","
              << s.cancel_p95_ms << ","
              << s.reject_rate << ","
              << s.maker_fill_rate << ","
              << s.taker_cost_bps << ","
              << s.spread_bps << ","
              << s.slippage_bps << ","
              << s.score() << ","
              << s.grade() << "\n";
        }
        
        f.close();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Pre-defined broker profiles (baseline reference)
// v4.11.0: binance removed - CFD only
// ─────────────────────────────────────────────────────────────────────────────
namespace Brokers {

inline BrokerScore blackbullBaseline() {
    BrokerScore s;
    strncpy(s.broker, "BLACKBULL", 31);
    strncpy(s.symbol, "XAUUSD", 15);
    s.ack_p50_ms = 3.0;
    s.ack_p80_ms = 5.0;
    s.ack_p95_ms = 8.0;
    s.cancel_p95_ms = 10.0;
    s.reject_rate = 0.05;
    s.maker_fill_rate = 0.60;
    s.taker_fill_rate = 0.95;
    s.taker_cost_bps = 4.0;
    s.maker_cost_bps = 1.5;
    s.spread_bps = 2.5;
    s.slippage_bps = 0.8;
    return s;
}

inline BrokerScore pepperstoneBaseline() {
    BrokerScore s;
    strncpy(s.broker, "PEPPERSTONE", 31);
    strncpy(s.symbol, "XAUUSD", 15);
    s.ack_p50_ms = 2.5;
    s.ack_p80_ms = 4.0;
    s.ack_p95_ms = 7.0;
    s.cancel_p95_ms = 9.0;
    s.reject_rate = 0.04;
    s.maker_fill_rate = 0.55;
    s.taker_fill_rate = 0.96;
    s.taker_cost_bps = 3.5;
    s.maker_cost_bps = 1.2;
    s.spread_bps = 2.0;
    s.slippage_bps = 0.6;
    return s;
}

} // namespace Brokers

} // namespace Audit
} // namespace Chimera
