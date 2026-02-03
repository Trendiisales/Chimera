#pragma once
#include <vector>
#include <cmath>
#include <deque>
#include <cstdint>
#include <chrono>

namespace chimera {

/**
 * Adverse Selection Detector
 * 
 * Detects when market makers are likely to be adversely selected (picked off).
 * Based on research from Easley, O'Hara, and others on order flow toxicity.
 * 
 * Features used:
 * 1. Order book imbalance (bid vs ask depth)
 * 2. Recent trade aggression (buy vs sell initiated)
 * 3. Quote update frequency (high frequency = informed traders)
 * 4. Spread volatility (widening = uncertainty)
 * 5. Price momentum (directional moves)
 * 6. Volume surge (unusual activity)
 * 
 * Output: Probability of adverse selection (0.0 to 1.0)
 * Usage: Skip market making when P(adverse) > threshold (e.g., 0.7)
 */
class AdverseSelectionDetector {
public:
    struct Config {
        int lookback_trades = 50;        // Number of recent trades to analyze
        int lookback_quotes = 100;       // Number of recent quotes to analyze
        double imbalance_threshold = 0.3; // 30% imbalance triggers concern
        double momentum_threshold = 2.0;  // 2 bps move = momentum signal
        double volume_surge_mult = 3.0;   // 3× average volume = surge
        double high_prob_threshold = 0.7; // Block trades above this
    };

    AdverseSelectionDetector()
        : cfg_()
        , ema_volume_(0.0)
        , ema_quote_freq_(0.0)
        , last_quote_time_ns_(0)
    {}

    explicit AdverseSelectionDetector(const Config& cfg)
        : cfg_(cfg)
        , ema_volume_(0.0)
        , ema_quote_freq_(0.0)
        , last_quote_time_ns_(0)
    {}

    /**
     * Update with new trade
     * @param price Trade price
     * @param size Trade size
     * @param is_buy_aggressor True if buyer initiated (took liquidity)
     */
    void on_trade(double price, double size, bool is_buy_aggressor) {
        uint64_t now = now_ns();
        
        recent_trades_.push_back({price, size, is_buy_aggressor, now});
        if (recent_trades_.size() > cfg_.lookback_trades) {
            recent_trades_.pop_front();
        }

        // Update volume EMA
        double alpha = 0.1;
        if (ema_volume_ == 0.0) {
            ema_volume_ = size;
        } else {
            ema_volume_ = alpha * size + (1.0 - alpha) * ema_volume_;
        }
    }

    /**
     * Update with new quote
     * @param bid Current best bid
     * @param ask Current best ask
     * @param bid_size Bid depth
     * @param ask_size Ask depth
     */
    void on_quote(double bid, double ask, double bid_size, double ask_size) {
        uint64_t now = now_ns();
        
        recent_quotes_.push_back({bid, ask, bid_size, ask_size, now});
        if (recent_quotes_.size() > cfg_.lookback_quotes) {
            recent_quotes_.pop_front();
        }

        // Update quote frequency EMA
        if (last_quote_time_ns_ > 0) {
            double dt_ms = (now - last_quote_time_ns_) / 1'000'000.0;
            double freq = 1000.0 / std::max(dt_ms, 1.0);  // quotes per second
            
            double alpha = 0.05;
            if (ema_quote_freq_ == 0.0) {
                ema_quote_freq_ = freq;
            } else {
                ema_quote_freq_ = alpha * freq + (1.0 - alpha) * ema_quote_freq_;
            }
        }
        last_quote_time_ns_ = now;
    }

    /**
     * Compute probability of adverse selection
     * @return Probability in [0.0, 1.0]
     */
    double compute_probability() {
        if (recent_trades_.empty() || recent_quotes_.empty()) {
            return 0.0;  // Not enough data
        }

        double score = 0.0;
        int num_signals = 0;

        // 1. Order book imbalance (25% weight)
        double imbalance = compute_imbalance();
        if (std::abs(imbalance) > cfg_.imbalance_threshold) {
            score += 0.25 * std::min(std::abs(imbalance), 1.0);
            num_signals++;
        }

        // 2. Trade aggression bias (25% weight)
        double aggression = compute_aggression_bias();
        if (std::abs(aggression) > 0.3) {
            score += 0.25 * std::min(std::abs(aggression), 1.0);
            num_signals++;
        }

        // 3. Quote update frequency (20% weight)
        if (ema_quote_freq_ > 10.0) {  // >10 updates/second = high activity
            double freq_signal = std::min(ema_quote_freq_ / 50.0, 1.0);
            score += 0.20 * freq_signal;
            num_signals++;
        }

        // 4. Price momentum (20% weight)
        double momentum = compute_momentum_bps();
        if (std::abs(momentum) > cfg_.momentum_threshold) {
            score += 0.20 * std::min(std::abs(momentum) / 10.0, 1.0);
            num_signals++;
        }

        // 5. Volume surge (10% weight)
        if (recent_trades_.back().size > ema_volume_ * cfg_.volume_surge_mult) {
            score += 0.10;
            num_signals++;
        }

        // Normalize by number of active signals
        if (num_signals > 0) {
            return std::min(score, 1.0);
        }
        
        return 0.0;
    }

    /**
     * Should we skip market making right now?
     */
    bool is_toxic() {
        return compute_probability() > cfg_.high_prob_threshold;
    }

    /**
     * Get detailed breakdown for logging
     */
    struct Breakdown {
        double probability;
        double imbalance;
        double aggression;
        double quote_freq;
        double momentum_bps;
        double volume_ratio;
    };

    Breakdown get_breakdown() {
        Breakdown b;
        b.probability = compute_probability();
        b.imbalance = compute_imbalance();
        b.aggression = compute_aggression_bias();
        b.quote_freq = ema_quote_freq_;
        b.momentum_bps = compute_momentum_bps();
        b.volume_ratio = recent_trades_.empty() ? 1.0 
                        : recent_trades_.back().size / std::max(ema_volume_, 0.01);
        return b;
    }

private:
    struct Trade {
        double price;
        double size;
        bool is_buy_aggressor;
        uint64_t timestamp_ns;
    };

    struct Quote {
        double bid;
        double ask;
        double bid_size;
        double ask_size;
        uint64_t timestamp_ns;
    };

    Config cfg_;
    std::deque<Trade> recent_trades_;
    std::deque<Quote> recent_quotes_;
    double ema_volume_;
    double ema_quote_freq_;
    uint64_t last_quote_time_ns_;

    /**
     * Order book imbalance: (bid_depth - ask_depth) / total_depth
     * Positive = more bids (upward pressure)
     * Negative = more asks (downward pressure)
     */
    double compute_imbalance() {
        if (recent_quotes_.empty()) return 0.0;
        
        const auto& q = recent_quotes_.back();
        double total = q.bid_size + q.ask_size;
        if (total < 0.001) return 0.0;
        
        return (q.bid_size - q.ask_size) / total;
    }

    /**
     * Trade aggression bias: (buy_volume - sell_volume) / total_volume
     * Positive = more aggressive buying
     * Negative = more aggressive selling
     */
    double compute_aggression_bias() {
        if (recent_trades_.empty()) return 0.0;
        
        double buy_vol = 0.0;
        double sell_vol = 0.0;
        
        for (const auto& t : recent_trades_) {
            if (t.is_buy_aggressor) {
                buy_vol += t.size;
            } else {
                sell_vol += t.size;
            }
        }
        
        double total = buy_vol + sell_vol;
        if (total < 0.001) return 0.0;
        
        return (buy_vol - sell_vol) / total;
    }

    /**
     * Price momentum in basis points over lookback window
     */
    double compute_momentum_bps() {
        if (recent_trades_.size() < 2) return 0.0;
        
        double first_price = recent_trades_.front().price;
        double last_price = recent_trades_.back().price;
        
        return ((last_price - first_price) / first_price) * 10000.0;
    }

    static uint64_t now_ns() {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(
            steady_clock::now().time_since_epoch()).count();
    }
};

} // namespace chimera
