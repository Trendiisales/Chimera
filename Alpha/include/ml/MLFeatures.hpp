// ═══════════════════════════════════════════════════════════════════════════════
// Alpha Trading System - ML Feature Extraction
// ═══════════════════════════════════════════════════════════════════════════════
// VERSION: 1.2.0
// PURPOSE: Extract ML-ready features from market microstructure for XAUUSD/NAS100
//
// FEATURES EXTRACTED:
// - Price momentum (fast/slow)
// - Volatility metrics (ATR ratio, realized vol)
// - Spread dynamics (normalized, percentile)
// - Order flow imbalance
// - Regime classification probabilities
// - Session context encoding
// - Position state features
//
// OUTPUT FORMAT: CSV with nanosecond precision timestamps
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include "../core/Types.hpp"
#include "../session/SessionDetector.hpp"
#include <array>
#include <cmath>
#include <string>
#include <sstream>
#include <iomanip>
#include <deque>
#include <mutex>
#include <atomic>

namespace Alpha {
namespace ML {

// ═══════════════════════════════════════════════════════════════════════════════
// FEATURE VECTOR DIMENSIONS
// ═══════════════════════════════════════════════════════════════════════════════
constexpr size_t NUM_PRICE_FEATURES = 12;
constexpr size_t NUM_VOL_FEATURES = 6;
constexpr size_t NUM_SPREAD_FEATURES = 4;
constexpr size_t NUM_FLOW_FEATURES = 6;
constexpr size_t NUM_REGIME_FEATURES = 5;
constexpr size_t NUM_SESSION_FEATURES = 4;
constexpr size_t NUM_POSITION_FEATURES = 8;
constexpr size_t TOTAL_FEATURES = NUM_PRICE_FEATURES + NUM_VOL_FEATURES + 
                                   NUM_SPREAD_FEATURES + NUM_FLOW_FEATURES + 
                                   NUM_REGIME_FEATURES + NUM_SESSION_FEATURES + 
                                   NUM_POSITION_FEATURES;

// ═══════════════════════════════════════════════════════════════════════════════
// RING BUFFER FOR HISTORICAL DATA
// ═══════════════════════════════════════════════════════════════════════════════
template<typename T, size_t N>
class RingBuffer {
public:
    void push(T value) noexcept {
        data_[head_] = value;
        head_ = (head_ + 1) % N;
        if (count_ < N) ++count_;
    }
    
    [[nodiscard]] T get(size_t ago) const noexcept {
        if (ago >= count_) return T{};
        size_t idx = (head_ + N - 1 - ago) % N;
        return data_[idx];
    }
    
    [[nodiscard]] T latest() const noexcept { return get(0); }
    [[nodiscard]] size_t size() const noexcept { return count_; }
    [[nodiscard]] bool full() const noexcept { return count_ == N; }
    
    void clear() noexcept { head_ = 0; count_ = 0; }
    
    // Statistics
    [[nodiscard]] double mean() const noexcept {
        if (count_ == 0) return 0.0;
        double sum = 0;
        for (size_t i = 0; i < count_; ++i) sum += data_[i];
        return sum / count_;
    }
    
    [[nodiscard]] double stdev() const noexcept {
        if (count_ < 2) return 0.0;
        double m = mean();
        double sq_sum = 0;
        for (size_t i = 0; i < count_; ++i) {
            double d = data_[i] - m;
            sq_sum += d * d;
        }
        return std::sqrt(sq_sum / (count_ - 1));
    }
    
    [[nodiscard]] double percentile(double p) const noexcept {
        if (count_ == 0) return 0.0;
        std::array<T, N> sorted;
        for (size_t i = 0; i < count_; ++i) sorted[i] = data_[i];
        std::sort(sorted.begin(), sorted.begin() + count_);
        size_t idx = static_cast<size_t>(p * (count_ - 1));
        return sorted[idx];
    }
    
private:
    std::array<T, N> data_{};
    size_t head_ = 0;
    size_t count_ = 0;
};

// ═══════════════════════════════════════════════════════════════════════════════
// FEATURE VECTOR
// ═══════════════════════════════════════════════════════════════════════════════
struct FeatureVector {
    uint64_t timestamp_ns = 0;
    Instrument instrument = Instrument::INVALID;
    std::array<double, TOTAL_FEATURES> features{};
    
    // Labels (for supervised learning - filled post-trade)
    double label_return_1m = 0.0;    // 1-minute forward return
    double label_return_5m = 0.0;    // 5-minute forward return
    int label_direction = 0;          // -1/0/+1
    double label_max_adverse = 0.0;   // Max adverse excursion
    double label_max_favorable = 0.0; // Max favorable excursion
    
    [[nodiscard]] std::string to_csv() const noexcept {
        std::ostringstream ss;
        ss << timestamp_ns << "," << instrument_str(instrument);
        for (size_t i = 0; i < TOTAL_FEATURES; ++i) {
            ss << "," << std::fixed << std::setprecision(6) << features[i];
        }
        ss << "," << label_return_1m << "," << label_return_5m
           << "," << label_direction << "," << label_max_adverse
           << "," << label_max_favorable;
        return ss.str();
    }
    
    static std::string csv_header() noexcept {
        std::ostringstream ss;
        ss << "timestamp_ns,instrument";
        
        // Price features
        ss << ",mom_fast,mom_slow,mom_delta,mom_accel";
        ss << ",price_z5,price_z20,price_z100";
        ss << ",ret_1,ret_5,ret_20,ret_100,ret_500";
        
        // Volatility features
        ss << ",atr_ratio,atr_raw,vol_realized,vol_z,vol_regime,vol_trend";
        
        // Spread features
        ss << ",spread_bps,spread_z,spread_pct,spread_regime";
        
        // Order flow features
        ss << ",flow_imb,flow_intensity,flow_z,tick_dir,uptick_pct,downtick_pct";
        
        // Regime features
        ss << ",regime_trend,regime_range,regime_vol,regime_quiet,regime_trans";
        
        // Session features
        ss << ",session_asia,session_london,session_ny,session_off";
        
        // Position features
        ss << ",has_pos,pos_side,pos_r,pos_hold_ms,pos_risk_free,pos_scaled,entry_edge,entry_spread";
        
        // Labels
        ss << ",label_ret_1m,label_ret_5m,label_dir,label_mae,label_mfe";
        
        return ss.str();
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// ML FEATURE EXTRACTOR
// ═══════════════════════════════════════════════════════════════════════════════
class FeatureExtractor {
public:
    explicit FeatureExtractor(Instrument inst) noexcept : instrument_(inst) {}
    
    void update(const Tick& tick) noexcept {
        if (tick.instrument != instrument_) return;
        
        ++tick_count_;
        
        // Store raw data
        prices_.push(tick.mid);
        spreads_.push(tick.spread_bps);
        timestamps_.push(tick.timestamp_ns);
        
        // Calculate returns
        if (prices_.size() > 1) {
            double prev = prices_.get(1);
            double ret = (tick.mid - prev) / prev * 10000.0;  // bps
            returns_.push(ret);
            
            // Tick direction
            int dir = (ret > 0.01) ? 1 : (ret < -0.01) ? -1 : 0;
            tick_dirs_.push(dir);
            
            // Uptick/downtick counts
            upticks_.push(dir > 0 ? 1.0 : 0.0);
            downticks_.push(dir < 0 ? 1.0 : 0.0);
        }
        
        // Momentum (EMA)
        if (returns_.size() > 0) {
            double ret = returns_.latest();
            mom_fast_ = 0.3 * ret + 0.7 * mom_fast_;
            mom_slow_ = 0.1 * ret + 0.9 * mom_slow_;
            mom_prev_ = mom_fast_;
        }
        
        // Volatility (EMA of absolute returns)
        if (returns_.size() > 0) {
            double abs_ret = std::abs(returns_.latest());
            vol_fast_ = 0.2 * abs_ret + 0.8 * vol_fast_;
            vol_slow_ = 0.05 * abs_ret + 0.95 * vol_slow_;
        }
        
        // ATR tracking
        if (prices_.size() >= 3) {
            double high = prices_.latest() * 1.0001;
            double low = prices_.latest() * 0.9999;
            double close = prices_.latest();
            double prev_close = prices_.get(1);
            
            double tr = std::max({
                high - low,
                std::abs(high - prev_close),
                std::abs(low - prev_close)
            });
            
            atr_ = 0.1 * tr + 0.9 * atr_;
            if (tick_count_ < 100) {
                baseline_atr_ = atr_;
            } else {
                baseline_atr_ = 0.001 * atr_ + 0.999 * baseline_atr_;
            }
        }
        
        last_tick_ = tick;
    }
    
    [[nodiscard]] FeatureVector extract() const noexcept {
        FeatureVector fv;
        fv.timestamp_ns = last_tick_.timestamp_ns;
        fv.instrument = instrument_;
        
        size_t idx = 0;
        
        // ═══════════════════════════════════════════════════════════════════
        // PRICE FEATURES (12)
        // ═══════════════════════════════════════════════════════════════════
        fv.features[idx++] = mom_fast_;                                    // mom_fast
        fv.features[idx++] = mom_slow_;                                    // mom_slow
        fv.features[idx++] = mom_fast_ - mom_slow_;                        // mom_delta
        fv.features[idx++] = mom_fast_ - mom_prev_;                        // mom_accel
        
        // Z-scores at different lookbacks
        fv.features[idx++] = z_score(prices_, 5);                          // price_z5
        fv.features[idx++] = z_score(prices_, 20);                         // price_z20
        fv.features[idx++] = z_score(prices_, 100);                        // price_z100
        
        // Returns at different lookbacks
        fv.features[idx++] = lookback_return(1);                           // ret_1
        fv.features[idx++] = lookback_return(5);                           // ret_5
        fv.features[idx++] = lookback_return(20);                          // ret_20
        fv.features[idx++] = lookback_return(100);                         // ret_100
        fv.features[idx++] = lookback_return(500);                         // ret_500
        
        // ═══════════════════════════════════════════════════════════════════
        // VOLATILITY FEATURES (6)
        // ═══════════════════════════════════════════════════════════════════
        double atr_ratio = (baseline_atr_ > 0) ? atr_ / baseline_atr_ : 1.0;
        fv.features[idx++] = atr_ratio;                                    // atr_ratio
        fv.features[idx++] = atr_;                                         // atr_raw
        fv.features[idx++] = vol_fast_;                                    // vol_realized
        fv.features[idx++] = (vol_slow_ > 0) ? vol_fast_ / vol_slow_ : 1.0; // vol_z
        fv.features[idx++] = classify_vol_regime();                        // vol_regime
        fv.features[idx++] = vol_fast_ - vol_slow_;                        // vol_trend
        
        // ═══════════════════════════════════════════════════════════════════
        // SPREAD FEATURES (4)
        // ═══════════════════════════════════════════════════════════════════
        double spread_mean = spreads_.mean();
        double spread_std = spreads_.stdev();
        double spread_z = (spread_std > 0) ? (last_tick_.spread_bps - spread_mean) / spread_std : 0;
        
        fv.features[idx++] = last_tick_.spread_bps;                        // spread_bps
        fv.features[idx++] = spread_z;                                     // spread_z
        fv.features[idx++] = percentile_rank(spreads_, last_tick_.spread_bps); // spread_pct
        fv.features[idx++] = classify_spread_regime();                     // spread_regime
        
        // ═══════════════════════════════════════════════════════════════════
        // ORDER FLOW FEATURES (6)
        // ═══════════════════════════════════════════════════════════════════
        double uptick_sum = 0, downtick_sum = 0;
        for (size_t i = 0; i < std::min(upticks_.size(), size_t(20)); ++i) {
            uptick_sum += upticks_.get(i);
            downtick_sum += downticks_.get(i);
        }
        double total_ticks = uptick_sum + downtick_sum;
        double flow_imb = (total_ticks > 0) ? (uptick_sum - downtick_sum) / total_ticks : 0;
        
        fv.features[idx++] = flow_imb;                                     // flow_imb
        fv.features[idx++] = total_ticks;                                  // flow_intensity
        fv.features[idx++] = flow_imb * std::sqrt(total_ticks);           // flow_z
        fv.features[idx++] = tick_dirs_.size() > 0 ? tick_dirs_.latest() : 0; // tick_dir
        fv.features[idx++] = (total_ticks > 0) ? uptick_sum / 20.0 : 0.5; // uptick_pct
        fv.features[idx++] = (total_ticks > 0) ? downtick_sum / 20.0 : 0.5; // downtick_pct
        
        // ═══════════════════════════════════════════════════════════════════
        // REGIME FEATURES (5) - One-hot encoded
        // ═══════════════════════════════════════════════════════════════════
        auto regime = classify_regime();
        fv.features[idx++] = (regime == 0) ? 1.0 : 0.0;                   // regime_trend
        fv.features[idx++] = (regime == 1) ? 1.0 : 0.0;                   // regime_range
        fv.features[idx++] = (regime == 2) ? 1.0 : 0.0;                   // regime_vol
        fv.features[idx++] = (regime == 3) ? 1.0 : 0.0;                   // regime_quiet
        fv.features[idx++] = (regime == 4) ? 1.0 : 0.0;                   // regime_trans
        
        // ═══════════════════════════════════════════════════════════════════
        // SESSION FEATURES (4) - One-hot encoded
        // ═══════════════════════════════════════════════════════════════════
        auto session = current_session_type();
        fv.features[idx++] = (session == SessionType::ASIA) ? 1.0 : 0.0;  // session_asia
        fv.features[idx++] = (session == SessionType::LONDON) ? 1.0 : 0.0; // session_london
        fv.features[idx++] = (session == SessionType::NY) ? 1.0 : 0.0;    // session_ny
        fv.features[idx++] = (session == SessionType::OFF) ? 1.0 : 0.0;   // session_off
        
        // ═══════════════════════════════════════════════════════════════════
        // POSITION FEATURES (8) - Filled externally
        // ═══════════════════════════════════════════════════════════════════
        // These are placeholders - actual values set by engine
        fv.features[idx++] = 0.0;  // has_pos
        fv.features[idx++] = 0.0;  // pos_side
        fv.features[idx++] = 0.0;  // pos_r
        fv.features[idx++] = 0.0;  // pos_hold_ms
        fv.features[idx++] = 0.0;  // pos_risk_free
        fv.features[idx++] = 0.0;  // pos_scaled
        fv.features[idx++] = 0.0;  // entry_edge
        fv.features[idx++] = 0.0;  // entry_spread
        
        return fv;
    }
    
    void set_position_features(FeatureVector& fv, bool has_pos, int side, double r,
                                uint64_t hold_ms, bool risk_free, bool scaled,
                                double entry_edge, double entry_spread) const noexcept {
        size_t base = NUM_PRICE_FEATURES + NUM_VOL_FEATURES + 
                      NUM_SPREAD_FEATURES + NUM_FLOW_FEATURES + 
                      NUM_REGIME_FEATURES + NUM_SESSION_FEATURES;
        
        fv.features[base + 0] = has_pos ? 1.0 : 0.0;
        fv.features[base + 1] = static_cast<double>(side);
        fv.features[base + 2] = r;
        fv.features[base + 3] = static_cast<double>(hold_ms) / 60000.0;  // Minutes
        fv.features[base + 4] = risk_free ? 1.0 : 0.0;
        fv.features[base + 5] = scaled ? 1.0 : 0.0;
        fv.features[base + 6] = entry_edge;
        fv.features[base + 7] = entry_spread;
    }
    
    [[nodiscard]] uint64_t tick_count() const noexcept { return tick_count_; }
    [[nodiscard]] bool ready() const noexcept { return tick_count_ >= 100; }
    
    void reset() noexcept {
        prices_.clear();
        spreads_.clear();
        returns_.clear();
        timestamps_.clear();
        tick_dirs_.clear();
        upticks_.clear();
        downticks_.clear();
        
        mom_fast_ = mom_slow_ = mom_prev_ = 0.0;
        vol_fast_ = vol_slow_ = 0.0;
        atr_ = baseline_atr_ = 0.0;
        tick_count_ = 0;
        last_tick_ = {};
    }

private:
    template<size_t N>
    [[nodiscard]] double z_score(const RingBuffer<double, N>& buf, size_t lookback) const noexcept {
        if (buf.size() < lookback) return 0.0;
        
        double sum = 0, sq_sum = 0;
        for (size_t i = 0; i < lookback; ++i) {
            double v = buf.get(i);
            sum += v;
            sq_sum += v * v;
        }
        double mean = sum / lookback;
        double var = sq_sum / lookback - mean * mean;
        double std = std::sqrt(std::max(0.0, var));
        
        if (std < 1e-10) return 0.0;
        return (buf.latest() - mean) / std;
    }
    
    template<size_t N>
    [[nodiscard]] double percentile_rank(const RingBuffer<double, N>& buf, double value) const noexcept {
        if (buf.size() == 0) return 0.5;
        
        size_t below = 0;
        for (size_t i = 0; i < buf.size(); ++i) {
            if (buf.get(i) < value) ++below;
        }
        return static_cast<double>(below) / buf.size();
    }
    
    [[nodiscard]] double lookback_return(size_t periods) const noexcept {
        if (prices_.size() <= periods) return 0.0;
        double curr = prices_.latest();
        double prev = prices_.get(periods);
        if (prev <= 0) return 0.0;
        return (curr - prev) / prev * 10000.0;  // bps
    }
    
    [[nodiscard]] int classify_regime() const noexcept {
        double vol = vol_fast_;
        double mom = std::abs(mom_fast_);
        double mom_diff = std::abs(mom_fast_ - mom_slow_);
        
        if (vol > 2.5) return 2;           // VOLATILE
        if (mom > 0.8 && mom_diff > 0.3) return 0;  // TRENDING
        if (vol < 0.5) return 3;           // QUIET
        if (mom < 0.4) return 1;           // RANGING
        return 4;                          // TRANSITION
    }
    
    [[nodiscard]] double classify_vol_regime() const noexcept {
        double atr_ratio = (baseline_atr_ > 0) ? atr_ / baseline_atr_ : 1.0;
        if (atr_ratio > 1.5) return 2.0;   // HIGH
        if (atr_ratio < 0.7) return 0.0;   // LOW
        return 1.0;                        // NORMAL
    }
    
    [[nodiscard]] double classify_spread_regime() const noexcept {
        double spread_pct = percentile_rank(spreads_, last_tick_.spread_bps);
        if (spread_pct > 0.8) return 2.0;  // WIDE
        if (spread_pct < 0.2) return 0.0;  // TIGHT
        return 1.0;                        // NORMAL
    }
    
    Instrument instrument_;
    Tick last_tick_;
    uint64_t tick_count_ = 0;
    
    // Historical data buffers
    RingBuffer<double, 1000> prices_;
    RingBuffer<double, 500> spreads_;
    RingBuffer<double, 500> returns_;
    RingBuffer<uint64_t, 100> timestamps_;
    RingBuffer<int, 100> tick_dirs_;
    RingBuffer<double, 100> upticks_;
    RingBuffer<double, 100> downticks_;
    
    // EMA state
    double mom_fast_ = 0.0, mom_slow_ = 0.0, mom_prev_ = 0.0;
    double vol_fast_ = 0.0, vol_slow_ = 0.0;
    double atr_ = 0.0, baseline_atr_ = 0.0;
};

// ═══════════════════════════════════════════════════════════════════════════════
// DUAL EXTRACTOR (for both instruments)
// ═══════════════════════════════════════════════════════════════════════════════
class DualFeatureExtractor {
public:
    DualFeatureExtractor() noexcept 
        : gold_extractor_(Instrument::XAUUSD)
        , nas_extractor_(Instrument::NAS100) 
    {}
    
    void update(const Tick& tick) noexcept {
        if (tick.instrument == Instrument::XAUUSD) {
            gold_extractor_.update(tick);
        } else if (tick.instrument == Instrument::NAS100) {
            nas_extractor_.update(tick);
        }
    }
    
    [[nodiscard]] FeatureVector extract(Instrument inst) const noexcept {
        if (inst == Instrument::XAUUSD) return gold_extractor_.extract();
        if (inst == Instrument::NAS100) return nas_extractor_.extract();
        return {};
    }
    
    [[nodiscard]] FeatureExtractor& extractor(Instrument inst) noexcept {
        return (inst == Instrument::XAUUSD) ? gold_extractor_ : nas_extractor_;
    }
    
    [[nodiscard]] const FeatureExtractor& extractor(Instrument inst) const noexcept {
        return (inst == Instrument::XAUUSD) ? gold_extractor_ : nas_extractor_;
    }
    
    [[nodiscard]] bool ready(Instrument inst) const noexcept {
        return extractor(inst).ready();
    }
    
private:
    FeatureExtractor gold_extractor_;
    FeatureExtractor nas_extractor_;
};

}  // namespace ML
}  // namespace Alpha
