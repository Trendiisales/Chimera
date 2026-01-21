#ifndef DEPTH_ENGINE_HPP
#define DEPTH_ENGINE_HPP

#include "engine_signal.hpp"
#include <atomic>
#include <cmath>
#include <algorithm>

class DepthEngine {
public:
    void ingest(double bid_depth, double ask_depth, uint64_t ts_ns) {
        if (baseline_bid_.load(std::memory_order_relaxed) == 0.0 && bid_depth > 0.0) {
            baseline_bid_.store(bid_depth, std::memory_order_relaxed);
            baseline_ask_.store(ask_depth, std::memory_order_relaxed);
        }
        
        double prev_bid = baseline_bid_.load(std::memory_order_relaxed);
        double prev_ask = baseline_ask_.load(std::memory_order_relaxed);
        
        baseline_bid_.store(0.995 * prev_bid + 0.005 * bid_depth, std::memory_order_relaxed);
        baseline_ask_.store(0.995 * prev_ask + 0.005 * ask_depth, std::memory_order_relaxed);
        
        current_bid_.store(bid_depth, std::memory_order_relaxed);
        current_ask_.store(ask_depth, std::memory_order_relaxed);
        
        double base_bid = baseline_bid_.load(std::memory_order_relaxed);
        double base_ask = baseline_ask_.load(std::memory_order_relaxed);
        
        double bid_ratio = (base_bid > 0.0) ? bid_depth / base_bid : 1.0;
        double ask_ratio = (base_ask > 0.0) ? ask_depth / base_ask : 1.0;
        double ratio = std::min(bid_ratio, ask_ratio);
        
        depth_ratio_.store(ratio, std::memory_order_relaxed);
        
        bool currently_collapsed = ratio < collapse_threshold_;
        bool was_collapsed = in_collapse_.load(std::memory_order_relaxed);
        
        if (currently_collapsed && !was_collapsed) {
            collapse_start_.store(ts_ns, std::memory_order_relaxed);
            in_collapse_.store(true, std::memory_order_relaxed);
        }
        else if (!currently_collapsed && was_collapsed) {
            in_collapse_.store(false, std::memory_order_relaxed);
            collapse_start_.store(0, std::memory_order_relaxed);
        }
        
        if (currently_collapsed) {
            uint64_t start = collapse_start_.load(std::memory_order_relaxed);
            collapse_duration_.store(ts_ns - start, std::memory_order_relaxed);
        } else {
            collapse_duration_.store(0, std::memory_order_relaxed);
        }
        
        last_ts_.store(ts_ns, std::memory_order_release);
    }

    DepthSignal evaluate(uint64_t now_ns) const {
        DepthSignal sig;
        sig.source = "DEPTH";
        sig.ts_ns = now_ns;
        
        double ratio = depth_ratio_.load(std::memory_order_acquire);
        uint64_t duration = collapse_duration_.load(std::memory_order_acquire);
        bool collapsed = in_collapse_.load(std::memory_order_acquire);
        
        sig.depth_ratio = ratio;
        sig.vacuum_duration_ns = duration;
        
        if (collapsed && duration >= min_vacuum_duration_ns_) {
            sig.fired = true;
            sig.side = Side::NONE;
            sig.confidence = std::min((1.0 - ratio) / 0.4, 1.0);
        }
        
        return sig;
    }

    double depthRatio() const { return depth_ratio_.load(std::memory_order_acquire); }
    bool inCollapse() const { return in_collapse_.load(std::memory_order_acquire); }
    uint64_t collapseDuration() const { return collapse_duration_.load(std::memory_order_acquire); }

    void setCollapseThreshold(double thresh) { collapse_threshold_ = thresh; }
    void setMinVacuumDuration(uint64_t ns) { min_vacuum_duration_ns_ = ns; }

private:
    std::atomic<double> baseline_bid_{0.0};
    std::atomic<double> baseline_ask_{0.0};
    std::atomic<double> current_bid_{0.0};
    std::atomic<double> current_ask_{0.0};
    std::atomic<double> depth_ratio_{1.0};
    
    std::atomic<bool> in_collapse_{false};
    std::atomic<uint64_t> collapse_start_{0};
    std::atomic<uint64_t> collapse_duration_{0};
    std::atomic<uint64_t> last_ts_{0};
    
    double collapse_threshold_ = 0.65;
    uint64_t min_vacuum_duration_ns_ = 300'000'000ULL;
};

#endif
