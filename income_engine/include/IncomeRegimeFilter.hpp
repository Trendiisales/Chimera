// =============================================================================
// IncomeRegimeFilter.hpp - ML-Based Regime Suitability Filter for Income Engine
// =============================================================================
// PURPOSE: Answer ONE question: "Is today/now a good time for income trading?"
//
// DESIGN PHILOSOPHY (from spec):
//   - ML does NOT predict price
//   - ML does NOT trigger trades
//   - ML does NOT size positions
//   - ML does NOT override rules
//   - ML ONLY outputs P(regime_suitable) for filtering
//
// FEATURES (all stationary, no future leakage):
//   - Realized volatility (1-5 min windows)
//   - Volatility percentile vs session
//   - Range compression ratio
//   - Spread stability
//   - Liquidity depth stability
//   - Impulse frequency
//   - Time-of-day encoding
//   - Crypto stress flag
//
// MODEL: Simple Gradient Boosted Trees / Logistic Regression
// OUTPUT: P(regime_suitable) in [0.0, 1.0]
// =============================================================================
#pragma once

#include <cmath>
#include <array>
#include <deque>
#include <chrono>
#include <algorithm>
#include <atomic>
#include <cstdint>

namespace Chimera {
namespace Income {

// =============================================================================
// Feature Vector for Regime Suitability
// =============================================================================
struct RegimeFeatures {
    // Volatility features
    double realized_vol_1m = 0.0;       // 1-minute realized vol (annualized)
    double realized_vol_5m = 0.0;       // 5-minute realized vol
    double vol_percentile = 0.5;        // Percentile vs session (0-1)
    double vol_ratio = 1.0;             // Current vol / median vol
    
    // Range/compression features
    double range_compression = 1.0;     // Current range / ATR (< 1 = compressed)
    double range_expansion = 1.0;       // Current range / recent low (> 1 = expanding)
    double atr_percentile = 0.5;        // ATR percentile vs history
    
    // Spread/liquidity features
    double spread_stability = 1.0;      // Spread stddev / median spread (< 1 = stable)
    double spread_percentile = 0.5;     // Current spread percentile
    double depth_stability = 1.0;       // Depth stddev / median depth
    double bid_ask_imbalance = 0.0;     // (bid_depth - ask_depth) / total (-1 to 1)
    
    // Activity features
    double impulse_frequency = 0.0;     // Large moves per minute
    double tick_rate = 0.0;             // Ticks per second (normalized)
    double trade_rate = 0.0;            // Trades per minute (normalized)
    
    // Time encoding (cyclical)
    double hour_sin = 0.0;              // sin(2π * hour / 24)
    double hour_cos = 0.0;              // cos(2π * hour / 24)
    double dow_sin = 0.0;               // sin(2π * dow / 7)
    double dow_cos = 0.0;               // cos(2π * dow / 7)
    
    // Cross-asset stress
    double crypto_stress = 0.0;         // Crypto volatility spike indicator (0-1)
    double vix_proxy = 0.0;             // Implied vol proxy (if available)
    
    // Microstructure
    double ofi_abs = 0.0;               // Absolute order flow imbalance
    double vpin_level = 0.0;            // Volume-synchronized PIN
    
    // Session indicators
    bool is_asia = false;
    bool is_london = false;
    bool is_ny = false;
    bool is_overlap = false;            // London-NY overlap (best liquidity)
    
    // Convert to array for model input (24 features)
    std::array<double, 24> to_array() const {
        return {{
            realized_vol_1m,
            realized_vol_5m,
            vol_percentile,
            vol_ratio,
            range_compression,
            range_expansion,
            atr_percentile,
            spread_stability,
            spread_percentile,
            depth_stability,
            bid_ask_imbalance,
            impulse_frequency,
            tick_rate,
            trade_rate,
            hour_sin,
            hour_cos,
            dow_sin,
            dow_cos,
            crypto_stress,
            vix_proxy,
            ofi_abs,
            vpin_level,
            is_asia ? 1.0 : 0.0,
            is_overlap ? 1.0 : 0.0
        }};
    }
};

// =============================================================================
// Rolling Statistics Calculator
// =============================================================================
class RollingStats {
public:
    explicit RollingStats(size_t window = 300) noexcept 
        : window_(window) {}
    
    void update(double value) noexcept {
        values_.push_back(value);
        if (values_.size() > window_) {
            values_.pop_front();
        }
        dirty_ = true;
    }
    
    double mean() const noexcept {
        if (values_.empty()) return 0.0;
        compute_if_dirty();
        return mean_;
    }
    
    double stddev() const noexcept {
        if (values_.size() < 2) return 0.0;
        compute_if_dirty();
        return stddev_;
    }
    
    double percentile(double value) const noexcept {
        if (values_.empty()) return 0.5;
        size_t count_below = 0;
        for (double v : values_) {
            if (v < value) count_below++;
        }
        return static_cast<double>(count_below) / values_.size();
    }
    
    double current_percentile() const noexcept {
        if (values_.empty()) return 0.5;
        return percentile(values_.back());
    }
    
    double min() const noexcept {
        if (values_.empty()) return 0.0;
        return *std::min_element(values_.begin(), values_.end());
    }
    
    double max() const noexcept {
        if (values_.empty()) return 0.0;
        return *std::max_element(values_.begin(), values_.end());
    }
    
    size_t size() const noexcept { return values_.size(); }
    bool ready() const noexcept { return values_.size() >= window_ / 2; }
    void clear() noexcept { values_.clear(); dirty_ = true; }
    
private:
    void compute_if_dirty() const noexcept {
        if (!dirty_) return;
        
        double sum = 0.0;
        for (double v : values_) sum += v;
        mean_ = sum / values_.size();
        
        double sq_sum = 0.0;
        for (double v : values_) {
            double d = v - mean_;
            sq_sum += d * d;
        }
        stddev_ = std::sqrt(sq_sum / values_.size());
        dirty_ = false;
    }
    
    std::deque<double> values_;
    size_t window_;
    mutable bool dirty_ = true;
    mutable double mean_ = 0.0;
    mutable double stddev_ = 0.0;
};

// =============================================================================
// Simple Logistic Regression Model (Fallback/Default)
// =============================================================================
// Weights trained offline on historical income trade outcomes
// Output: P(regime_suitable) via sigmoid
class LogisticRegimeModel {
public:
    // Pre-trained weights (would be loaded from file in production)
    // These are reasonable defaults that favor low-vol, stable spread conditions
    static constexpr std::array<double, 24> DEFAULT_WEIGHTS = {{
        -0.5,   // realized_vol_1m (higher vol = worse)
        -0.3,   // realized_vol_5m
        -0.2,   // vol_percentile (high percentile = worse)
        -0.4,   // vol_ratio (high ratio = worse)
        0.3,    // range_compression (compressed = good for income)
        -0.3,   // range_expansion (expanding = bad)
        -0.2,   // atr_percentile
        0.4,    // spread_stability (stable = good)
        -0.3,   // spread_percentile (wide spread = bad)
        0.3,    // depth_stability
        -0.1,   // bid_ask_imbalance (extreme imbalance = bad)
        -0.4,   // impulse_frequency (many impulses = bad)
        0.1,    // tick_rate (some activity = good)
        0.1,    // trade_rate
        0.1,    // hour_sin
        0.1,    // hour_cos
        0.0,    // dow_sin
        0.0,    // dow_cos
        -0.6,   // crypto_stress (stress = very bad)
        -0.3,   // vix_proxy
        -0.2,   // ofi_abs
        -0.2,   // vpin_level
        -0.1,   // is_asia (slightly worse liquidity)
        0.3     // is_overlap (best liquidity = good)
    }};
    
    static constexpr double DEFAULT_BIAS = 0.5;
    
    LogisticRegimeModel() noexcept 
        : weights_(DEFAULT_WEIGHTS), bias_(DEFAULT_BIAS) {}
    
    // Load custom weights from array
    void load_weights(const std::array<double, 24>& w, double b) noexcept {
        weights_ = w;
        bias_ = b;
    }
    
    // Predict P(regime_suitable)
    double predict(const RegimeFeatures& features) const noexcept {
        auto x = features.to_array();
        double logit = bias_;
        for (size_t i = 0; i < 24; i++) {
            logit += weights_[i] * x[i];
        }
        return sigmoid(logit);
    }
    
    // Threshold for "suitable"
    bool is_suitable(const RegimeFeatures& features, double threshold = 0.55) const noexcept {
        return predict(features) >= threshold;
    }
    
private:
    static double sigmoid(double x) noexcept {
        return 1.0 / (1.0 + std::exp(-x));
    }
    
    std::array<double, 24> weights_;
    double bias_;
};

// =============================================================================
// Decision Tree Node (for Gradient Boosted Trees)
// =============================================================================
struct TreeNode {
    int feature_idx = -1;       // -1 = leaf
    double threshold = 0.0;
    double leaf_value = 0.0;
    int left_child = -1;
    int right_child = -1;
    
    bool is_leaf() const noexcept { return feature_idx < 0; }
};

// =============================================================================
// Single Decision Tree
// =============================================================================
class DecisionTree {
public:
    static constexpr size_t MAX_NODES = 127;  // Max depth ~6
    
    DecisionTree() noexcept : node_count_(0) {}
    
    void add_node(const TreeNode& node) noexcept {
        if (node_count_ < MAX_NODES) {
            nodes_[node_count_++] = node;
        }
    }
    
    double predict(const std::array<double, 24>& x) const noexcept {
        if (node_count_ == 0) return 0.0;
        
        int idx = 0;
        while (!nodes_[idx].is_leaf()) {
            int feat = nodes_[idx].feature_idx;
            if (x[feat] <= nodes_[idx].threshold) {
                idx = nodes_[idx].left_child;
            } else {
                idx = nodes_[idx].right_child;
            }
            if (idx < 0 || idx >= static_cast<int>(node_count_)) break;
        }
        return nodes_[idx].leaf_value;
    }
    
    size_t size() const noexcept { return node_count_; }
    
private:
    std::array<TreeNode, MAX_NODES> nodes_;
    size_t node_count_;
};

// =============================================================================
// Gradient Boosted Trees Ensemble
// =============================================================================
class GBTRegimeModel {
public:
    static constexpr size_t MAX_TREES = 50;
    
    GBTRegimeModel() noexcept 
        : tree_count_(0), learning_rate_(0.1), base_score_(0.0) {}
    
    void set_base_score(double s) noexcept { base_score_ = s; }
    void set_learning_rate(double lr) noexcept { learning_rate_ = lr; }
    
    void add_tree(const DecisionTree& tree) noexcept {
        if (tree_count_ < MAX_TREES) {
            trees_[tree_count_++] = tree;
        }
    }
    
    double predict_raw(const RegimeFeatures& features) const noexcept {
        auto x = features.to_array();
        double pred = base_score_;
        for (size_t i = 0; i < tree_count_; i++) {
            pred += learning_rate_ * trees_[i].predict(x);
        }
        return pred;
    }
    
    // Convert to probability via sigmoid
    double predict(const RegimeFeatures& features) const noexcept {
        return 1.0 / (1.0 + std::exp(-predict_raw(features)));
    }
    
    bool is_suitable(const RegimeFeatures& features, double threshold = 0.55) const noexcept {
        return predict(features) >= threshold;
    }
    
    size_t tree_count() const noexcept { return tree_count_; }
    
private:
    std::array<DecisionTree, MAX_TREES> trees_;
    size_t tree_count_;
    double learning_rate_;
    double base_score_;
};

// =============================================================================
// Income Regime Filter - Main Interface
// =============================================================================
class IncomeRegimeFilter {
public:
    enum class ModelType : uint8_t {
        LOGISTIC = 0,
        GBT = 1
    };
    
    struct Config {
        double suitability_threshold = 0.60;    // FIXED at 0.60 - do not change
        double high_confidence_threshold = 0.75; // High confidence for full size
        double min_samples = 100;               // Min samples before predictions valid
        int vol_window = 300;                   // Volatility rolling window (ticks)
        int spread_window = 100;                // Spread rolling window
        int depth_window = 100;                 // Depth rolling window
        ModelType model_type = ModelType::LOGISTIC;
    };
    
    IncomeRegimeFilter() noexcept 
        : IncomeRegimeFilter(Config()) {}
    
    explicit IncomeRegimeFilter(const Config& cfg) noexcept
        : config_(cfg)
        , vol_stats_(cfg.vol_window)
        , spread_stats_(cfg.spread_window)
        , depth_stats_(cfg.depth_window)
        , range_stats_(cfg.vol_window)
        , impulse_count_(0)
        , tick_count_(0)
        , last_price_(0.0)
        , sample_count_(0)
    {}
    
    // =========================================================================
    // Tick Update - Call on every price update
    // =========================================================================
    void on_tick(
        double price,
        double spread,
        double bid_depth,
        double ask_depth,
        double ofi,
        double vpin,
        uint64_t timestamp_ns
    ) noexcept {
        sample_count_++;
        tick_count_++;
        
        // Update volatility (log returns)
        if (last_price_ > 0.0) {
            double ret = std::log(price / last_price_);
            vol_stats_.update(std::abs(ret));
            
            // Detect impulse (> 2 stddev move)
            double vol = vol_stats_.stddev();
            if (vol > 0.0 && std::abs(ret) > 2.0 * vol) {
                impulse_count_++;
            }
        }
        last_price_ = price;
        
        // Update spread stats
        spread_stats_.update(spread);
        
        // Update depth stats
        double total_depth = bid_depth + ask_depth;
        if (total_depth > 0.0) {
            depth_stats_.update(total_depth);
            current_imbalance_ = (bid_depth - ask_depth) / total_depth;
        }
        
        // Update OFI/VPIN
        current_ofi_ = ofi;
        current_vpin_ = vpin;
        
        // Update time tracking
        last_timestamp_ns_ = timestamp_ns;
    }
    
    // Update range (high/low over period)
    void on_range(double range, double atr) noexcept {
        range_stats_.update(range);
        current_atr_ = atr;
    }
    
    // Update crypto stress indicator
    void set_crypto_stress(double stress) noexcept {
        crypto_stress_.store(stress);
    }
    
    // =========================================================================
    // Compute Features and Predict
    // =========================================================================
    RegimeFeatures compute_features() const noexcept {
        RegimeFeatures f;
        
        // Volatility features
        double vol = vol_stats_.stddev();
        f.realized_vol_1m = vol * std::sqrt(60.0 * 252.0);  // Annualized
        f.realized_vol_5m = vol * std::sqrt(12.0 * 252.0);
        f.vol_percentile = vol_stats_.current_percentile();
        double mean_vol = vol_stats_.mean();
        f.vol_ratio = (mean_vol > 0.0) ? (vol / mean_vol) : 1.0;
        
        // Range features
        double mean_range = range_stats_.mean();
        if (current_atr_ > 0.0) {
            f.range_compression = mean_range / current_atr_;
        }
        double min_range = range_stats_.min();
        if (min_range > 0.0) {
            f.range_expansion = mean_range / min_range;
        }
        f.atr_percentile = range_stats_.current_percentile();
        
        // Spread/liquidity features
        double spread_mean = spread_stats_.mean();
        double spread_std = spread_stats_.stddev();
        f.spread_stability = (spread_mean > 0.0) ? (spread_std / spread_mean) : 1.0;
        f.spread_percentile = spread_stats_.current_percentile();
        
        double depth_mean = depth_stats_.mean();
        double depth_std = depth_stats_.stddev();
        f.depth_stability = (depth_mean > 0.0) ? (depth_std / depth_mean) : 1.0;
        f.bid_ask_imbalance = current_imbalance_;
        
        // Activity features
        // Normalize impulse frequency to per-minute
        double minutes = static_cast<double>(tick_count_) / 60.0;  // Assume 1 tick/sec
        f.impulse_frequency = (minutes > 0.0) ? (impulse_count_ / minutes) : 0.0;
        f.tick_rate = 1.0;  // Normalized
        f.trade_rate = 1.0;
        
        // Time encoding
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm* utc = std::gmtime(&time);
        if (utc) {
            double hour = utc->tm_hour + utc->tm_min / 60.0;
            double dow = utc->tm_wday;
            f.hour_sin = std::sin(2.0 * M_PI * hour / 24.0);
            f.hour_cos = std::cos(2.0 * M_PI * hour / 24.0);
            f.dow_sin = std::sin(2.0 * M_PI * dow / 7.0);
            f.dow_cos = std::cos(2.0 * M_PI * dow / 7.0);
            
            // Session detection
            int h = utc->tm_hour;
            f.is_asia = (h >= 0 && h < 8);
            f.is_london = (h >= 8 && h < 16);
            f.is_ny = (h >= 13 && h < 21);
            f.is_overlap = (h >= 13 && h < 16);  // London-NY overlap
        }
        
        // Cross-asset stress
        f.crypto_stress = crypto_stress_.load();
        f.vix_proxy = 0.0;  // Would need VIX feed
        
        // Microstructure
        f.ofi_abs = std::abs(current_ofi_);
        f.vpin_level = current_vpin_;
        
        return f;
    }
    
    // Get P(regime_suitable)
    double suitability_score() const noexcept {
        if (!is_warmed_up()) return 0.0;
        
        RegimeFeatures f = compute_features();
        
        if (config_.model_type == ModelType::GBT && gbt_model_.tree_count() > 0) {
            return gbt_model_.predict(f);
        }
        return logistic_model_.predict(f);
    }
    
    // Is regime suitable for income trading?
    bool is_suitable() const noexcept {
        return suitability_score() >= config_.suitability_threshold;
    }
    
    // Is regime highly suitable (for full size)?
    bool is_high_confidence() const noexcept {
        return suitability_score() >= config_.high_confidence_threshold;
    }
    
    // Get size scalar based on confidence (0.5 - 1.0)
    double size_scalar() const noexcept {
        double score = suitability_score();
        if (score < config_.suitability_threshold) return 0.0;
        
        // Linear interpolation from 0.5 at threshold to 1.0 at high_confidence
        double range = config_.high_confidence_threshold - config_.suitability_threshold;
        if (range <= 0.0) return 1.0;
        
        double frac = (score - config_.suitability_threshold) / range;
        return 0.5 + 0.5 * std::min(frac, 1.0);
    }
    
    // =========================================================================
    // Accessors
    // =========================================================================
    bool is_warmed_up() const noexcept {
        return sample_count_ >= static_cast<uint64_t>(config_.min_samples);
    }
    
    uint64_t sample_count() const noexcept { return sample_count_; }
    
    Config& config() noexcept { return config_; }
    const Config& config() const noexcept { return config_; }
    
    // Model access (for loading trained weights)
    LogisticRegimeModel& logistic_model() noexcept { return logistic_model_; }
    GBTRegimeModel& gbt_model() noexcept { return gbt_model_; }
    
    void reset() noexcept {
        vol_stats_.clear();
        spread_stats_.clear();
        depth_stats_.clear();
        range_stats_.clear();
        impulse_count_ = 0;
        tick_count_ = 0;
        last_price_ = 0.0;
        sample_count_ = 0;
    }
    
private:
    Config config_;
    
    // Rolling statistics
    RollingStats vol_stats_;
    RollingStats spread_stats_;
    RollingStats depth_stats_;
    RollingStats range_stats_;
    
    // Current state
    uint64_t impulse_count_;
    uint64_t tick_count_;
    double last_price_;
    uint64_t sample_count_;
    uint64_t last_timestamp_ns_ = 0;
    
    double current_imbalance_ = 0.0;
    double current_ofi_ = 0.0;
    double current_vpin_ = 0.0;
    double current_atr_ = 0.0;
    
    std::atomic<double> crypto_stress_{0.0};
    
    // Models
    LogisticRegimeModel logistic_model_;
    GBTRegimeModel gbt_model_;
};

} // namespace Income
} // namespace Chimera
