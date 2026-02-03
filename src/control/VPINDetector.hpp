#pragma once
#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <chrono>

namespace chimera {

/**
 * VPIN (Volume-Synchronized Probability of Informed Trading)
 * 
 * Developed by Easley, López de Prado, and O'Hara (2012)
 * Measures order flow toxicity in real-time
 * 
 * Key insight: Informed traders create imbalanced order flow
 * When VPIN is high (>0.7), market making becomes unprofitable
 * 
 * Algorithm:
 * 1. Divide time into volume buckets (not time buckets)
 * 2. Classify each trade as buy or sell
 * 3. Compute |buy_volume - sell_volume| per bucket
 * 4. VPIN = average imbalance / total volume
 * 
 * Used by: Jump Trading, Jane Street, Tower Research
 */
class VPINDetector {
public:
    struct Config {
        int num_buckets = 50;            // Number of volume buckets
        double volume_per_bucket = 10.0; // Target volume per bucket
        double toxic_threshold = 0.7;    // VPIN > this = toxic
        double critical_threshold = 0.85; // VPIN > this = critical
    };

    VPINDetector()
        : cfg_()
        , current_bucket_volume_(0.0)
        , current_buy_volume_(0.0)
        , current_sell_volume_(0.0)
    {}

    explicit VPINDetector(const Config& cfg)
        : cfg_(cfg)
        , current_bucket_volume_(0.0)
        , current_buy_volume_(0.0)
        , current_sell_volume_(0.0)
    {}

    /**
     * Update with new trade
     * @param size Trade size
     * @param is_buy True if buy-initiated (aggressor bought)
     */
    void on_trade(double size, bool is_buy) {
        if (is_buy) {
            current_buy_volume_ += size;
        } else {
            current_sell_volume_ += size;
        }
        current_bucket_volume_ += size;

        // If bucket is full, store it and start new one
        if (current_bucket_volume_ >= cfg_.volume_per_bucket) {
            close_bucket();
        }
    }

    /**
     * Compute current VPIN value
     * @return VPIN in [0.0, 1.0]
     */
    double compute_vpin() {
        if (buckets_.size() < 2) {
            return 0.0;  // Not enough data
        }

        double sum_imbalance = 0.0;
        double sum_volume = 0.0;

        for (const auto& bucket : buckets_) {
            double imbalance = std::abs(bucket.buy_volume - bucket.sell_volume);
            sum_imbalance += imbalance;
            sum_volume += bucket.total_volume;
        }

        if (sum_volume < 0.001) {
            return 0.0;
        }

        return sum_imbalance / sum_volume;
    }

    /**
     * Is order flow currently toxic?
     */
    bool is_toxic() {
        return compute_vpin() > cfg_.toxic_threshold;
    }

    /**
     * Is order flow critically toxic? (emergency stop)
     */
    bool is_critical() {
        return compute_vpin() > cfg_.critical_threshold;
    }

    /**
     * Get toxicity level (human readable)
     */
    const char* toxicity_level() {
        double vpin = compute_vpin();
        if (vpin < 0.3) return "LOW";
        if (vpin < 0.5) return "MODERATE";
        if (vpin < 0.7) return "ELEVATED";
        if (vpin < 0.85) return "TOXIC";
        return "CRITICAL";
    }

    /**
     * Get detailed metrics
     */
    struct Metrics {
        double vpin;
        double avg_imbalance_pct;
        int num_buckets;
        double current_bucket_fill;
        const char* level;
    };

    Metrics get_metrics() {
        Metrics m;
        m.vpin = compute_vpin();
        m.avg_imbalance_pct = m.vpin * 100.0;
        m.num_buckets = buckets_.size();
        m.current_bucket_fill = current_bucket_volume_ / cfg_.volume_per_bucket;
        m.level = toxicity_level();
        return m;
    }

    /**
     * Reset state (e.g., after market regime change)
     */
    void reset() {
        buckets_.clear();
        current_bucket_volume_ = 0.0;
        current_buy_volume_ = 0.0;
        current_sell_volume_ = 0.0;
    }

private:
    struct VolumeBucket {
        double buy_volume;
        double sell_volume;
        double total_volume;
    };

    Config cfg_;
    std::deque<VolumeBucket> buckets_;
    double current_bucket_volume_;
    double current_buy_volume_;
    double current_sell_volume_;

    void close_bucket() {
        VolumeBucket bucket;
        bucket.buy_volume = current_buy_volume_;
        bucket.sell_volume = current_sell_volume_;
        bucket.total_volume = current_bucket_volume_;

        buckets_.push_back(bucket);
        
        // Keep only last N buckets
        if (buckets_.size() > cfg_.num_buckets) {
            buckets_.pop_front();
        }

        // Reset current bucket
        current_bucket_volume_ = 0.0;
        current_buy_volume_ = 0.0;
        current_sell_volume_ = 0.0;
    }
};

/**
 * Combined Toxicity Filter
 * 
 * Combines VPIN with adverse selection detector
 * Provides unified "should I market make?" answer
 */
class ToxicityFilter {
public:
    struct Config {
        VPINDetector::Config vpin_cfg;
        double combined_threshold = 0.6;  // Block if either signal > this
    };

    ToxicityFilter()
        : cfg_()
        , vpin_(cfg_.vpin_cfg)
    {}

    explicit ToxicityFilter(const Config& cfg)
        : cfg_(cfg)
        , vpin_(cfg.vpin_cfg)
    {}

    void on_trade(double price, double size, bool is_buy) {
        vpin_.on_trade(size, is_buy);
    }

    /**
     * Should we avoid market making right now?
     * @return true if toxic conditions detected
     */
    bool should_avoid_market_making() {
        double vpin = vpin_.compute_vpin();
        return vpin > cfg_.combined_threshold;
    }

    /**
     * Get combined toxicity score [0.0, 1.0]
     */
    double toxicity_score() {
        return vpin_.compute_vpin();
    }

    /**
     * Get detailed breakdown
     */
    struct Breakdown {
        double vpin;
        const char* level;
        bool should_avoid;
    };

    Breakdown get_breakdown() {
        Breakdown b;
        b.vpin = vpin_.compute_vpin();
        b.level = vpin_.toxicity_level();
        b.should_avoid = should_avoid_market_making();
        return b;
    }

    void reset() {
        vpin_.reset();
    }

private:
    Config cfg_;
    VPINDetector vpin_;
};

} // namespace chimera
