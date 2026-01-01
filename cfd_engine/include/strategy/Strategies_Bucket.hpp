// =============================================================================
// Strategies_Bucket.hpp - 10-Bucket Strategy System v3.0 (AUDIT FIXES)
// =============================================================================
// FIXES IMPLEMENTED:
//   1. Correlation penalty across B1/B2/B6 (reduces fake confidence)
//   2. OFI confidence decay when spread/vol/liquidity degrade
//   3. Momentum trend persistence check (penalize flip-flop)
//   4. Liquidity vacuum = RISK MODIFIER, not directional signal
//   5. Mean reversion disabled in trending/volatile regimes
//   6. Spread as HARD VETO (not just observed)
//   7. Volatility drives position sizing via Q_vol
//   8. Latency as HARD VETO
//   9. Normalized scores [-1,+1] consistently
//   10. Unified risk scaler integration
//
// OUTPUT CONTRACT:
//   - signal_dir: -1, 0, +1 (direction)
//   - signal_abs: [0,1] (strength)
//   - confidence: [0,1] (reliability - independent of strength)
// =============================================================================
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include "../data/UnifiedTick.hpp"
#include "../micro/CentralMicroEngine.hpp"

namespace Omega {

// =============================================================================
// Strategy Signal - Clean output contract
// =============================================================================
struct StrategySignal {
    int8_t dir = 0;           // -1, 0, +1
    double signal_abs = 0.0;  // [0,1] strength
    double confidence = 0.0;  // [0,1] reliability
    
    // v6.68: RELAXED thresholds for live demo - was 0.1/0.05
    inline bool isActive() const { return confidence > 0.05 && signal_abs > 0.02; }
    inline bool isBuy() const { return dir > 0 && isActive(); }
    inline bool isSell() const { return dir < 0 && isActive(); }
};

// =============================================================================
// Bucket IDs
// =============================================================================
enum class BucketId : uint8_t {
    ORDER_FLOW      = 0,   // B1 - directional
    MOMENTUM        = 1,   // B2 - directional
    LIQUIDITY       = 2,   // B3 - risk modifier
    REVERSION       = 3,   // B4 - directional (regime-conditional)
    SPREAD_REGIME   = 4,   // B5 - veto/suppressor
    AGGRESSION      = 5,   // B6 - directional
    VOLATILITY      = 6,   // B7 - risk modifier
    EXEC_SAFETY     = 7,   // B8 - veto/suppressor
    SESSION_BIAS    = 8,   // B9 - risk modifier
    CONFIRMATION    = 9,   // B10 - gate
    NUM_BUCKETS     = 10
};

// =============================================================================
// Regime State - Shared context for all strategies
// =============================================================================
struct RegimeState {
    double vol_z = 1.0;           // fast_vol / slow_vol
    double spread_z = 1.0;        // spread / median
    double liq_z = 1.0;           // depth / median
    double lat_z = 1.0;           // latency / baseline
    double health = 1.0;          // feed health [0,1]
    double session = 1.0;         // session quality [0,1]
    bool is_trending = false;
    bool is_volatile = false;
    int utc_hour = 12;
};

// =============================================================================
// Bucket Weights and Config
// =============================================================================
struct BucketWeights {
    std::array<double, 10> signalWeights = {{
        1.0, 1.0, 0.0, 0.6, 0.0, 0.9, 0.0, 0.0, 0.0, 1.0
    }};
    std::array<double, 10> riskWeights = {{
        0.3, 0.2, -0.4, 0.0, -0.5, 0.3, 0.0, 0.0, 0.2, 0.1
    }};
    std::array<bool, 10> canVeto = {{
        false, false, false, false, true, false, false, true, false, false
    }};
};

struct BucketConfig {
    // Weights for directional voting (0 = non-directional bucket)
    // v6.72: FIXED for CFD - disabled volume-dependent strategies
    std::array<double, 10> dir_weights = {{
        0.0,   // B1 - OFI - DISABLED (needs volume data we don't have)
        2.0,   // B2 - Momentum - BOOSTED (price-based, works!)
        0.0,   // B3 - Liquidity (risk modifier only)
        1.5,   // B4 - Reversion - BOOSTED (price-based)
        0.0,   // B5 - Spread (veto only)
        0.0,   // B6 - Aggression - DISABLED (needs volume data)
        0.0,   // B7 - Volatility (risk modifier only)
        0.0,   // B8 - Latency (veto only)
        0.5,   // B9 - Session - ENABLED (time-based bias)
        1.0    // B10 - Confirmation
    }};
    
    // Thresholds - v6.72 RELAXED
    double spread_veto_z = 5.0;    // Was 2.0 - now more tolerant
    double latency_veto_ns = 500000; // Was 150μs - now 500μs tolerance
    double vol_expand_z = 2.0;     // Was 1.5 - more tolerance
    double trend_strength = 0.3;   // Was 0.5 - easier to detect trends
};

// =============================================================================
// B1: Order Flow Imbalance (OFI) - WITH REGIME CONDITIONING
// =============================================================================
class OFIStrategy {
    static constexpr size_t WINDOW = 32;
    static constexpr double OFI_NORM = 0.6;
    
public:
    OFIStrategy() { reset(); }
    
    StrategySignal compute(const UnifiedTick& t, const MicrostructureSignals& sig, const RegimeState& regime) {
        // Delta order flow
        double buy_flow = t.buyVol - last_buy_;
        double sell_flow = t.sellVol - last_sell_;
        last_buy_ = t.buyVol;
        last_sell_ = t.sellVol;
        
        double total = buy_flow + sell_flow + 1e-9;
        double raw = (buy_flow - sell_flow) / total;
        
        // EMA smoothing
        ofi_ema_ = 0.85 * ofi_ema_ + 0.15 * raw;
        
        // Update depth tracking
        depth_ema_ = 0.95 * depth_ema_ + 0.05 * (sig.depthImbalance != 0 ? 1.0 : 0.5);
        spread_ema_ = 0.95 * spread_ema_ + 0.05 * t.spread;
        
        StrategySignal s;
        s.dir = (ofi_ema_ > 0.05) ? 1 : (ofi_ema_ < -0.05) ? -1 : 0;
        s.signal_abs = std::clamp(std::fabs(ofi_ema_) / OFI_NORM, 0.0, 1.0);
        
        // CONFIDENCE: Decays when regime degrades
        // conf = (depth/depth_median) × (spread_median/spread)
        double depth_factor = std::clamp(regime.liq_z, 0.2, 1.5);
        double spread_factor = (regime.spread_z > 0.1) ? std::clamp(1.0 / regime.spread_z, 0.2, 1.5) : 1.0;
        double vol_factor = (regime.vol_z > 1.0) ? std::clamp(1.0 / regime.vol_z, 0.3, 1.0) : 1.0;
        
        s.confidence = std::clamp(depth_factor * spread_factor * vol_factor, 0.0, 1.0);
        
        return s;
    }
    
    void reset() {
        last_buy_ = 0; last_sell_ = 0; ofi_ema_ = 0;
        depth_ema_ = 1.0; spread_ema_ = 0.0001;
    }
    
private:
    double last_buy_, last_sell_, ofi_ema_;
    double depth_ema_, spread_ema_;
};

// =============================================================================
// B2: Micro Momentum - WITH FLIP-FLOP PENALTY
// =============================================================================
class MomentumStrategy {
    static constexpr size_t WINDOW = 32;
    static constexpr size_t FLIP_WINDOW = 16;
    
public:
    MomentumStrategy() { reset(); }
    
    StrategySignal compute(const UnifiedTick& t, const MicrostructureSignals& /*sig*/, const RegimeState&) {
        double mid = 0.5 * (t.bid + t.ask);
        double delta = mid - last_mid_;
        last_mid_ = mid;
        
        // ATR for normalization
        atr_ = 0.95 * atr_ + 0.05 * std::fabs(delta);
        double atr_safe = std::max(atr_, 1e-8);
        
        double raw = delta / atr_safe;
        trend_ = 0.92 * trend_ + 0.08 * raw;
        
        StrategySignal s;
        s.dir = (trend_ > 0.1) ? 1 : (trend_ < -0.1) ? -1 : 0;
        s.signal_abs = std::clamp(std::fabs(trend_), 0.0, 1.0);
        
        // FLIP-FLOP PENALTY: Track direction changes
        if (s.dir != 0 && s.dir != last_dir_ && last_dir_ != 0) {
            flip_count_ = std::min(flip_count_ + 1, (int)FLIP_WINDOW);
        } else if (flip_count_ > 0) {
            flip_count_--;
        }
        last_dir_ = s.dir;
        
        double flip_rate = flip_count_ / (double)FLIP_WINDOW;
        s.confidence = std::clamp(1.0 - flip_rate, 0.0, 1.0);
        
        return s;
    }
    
    void reset() {
        last_mid_ = 0; trend_ = 0; atr_ = 0.0001;
        flip_count_ = 0; last_dir_ = 0;
    }
    
private:
    double last_mid_, trend_, atr_;
    int flip_count_;
    int8_t last_dir_;
};

// =============================================================================
// B3: Liquidity Vacuum - RISK MODIFIER ONLY (NON-DIRECTIONAL)
// =============================================================================
class LiquidityVacuumStrategy {
public:
    LiquidityVacuumStrategy() { reset(); }
    
    StrategySignal compute(const UnifiedTick& t, const MicrostructureSignals& /*sig*/, const RegimeState& regime) {
        // Track spread expansion
        spread_ema_ = 0.95 * spread_ema_ + 0.05 * t.spread;
        
        // Liquidity stress = how thin is the book?
        double L = regime.liq_z;  // depth / median
        stress_ = std::clamp((1.0 - L) / 0.5, 0.0, 1.0);
        
        StrategySignal s;
        s.dir = 0;  // NEVER directional - vacuum doesn't tell us which way
        s.signal_abs = stress_;
        
        // Confidence based on spread quality
        double spread_quality = (spread_ema_ > 0) ? std::clamp(spread_ema_ / t.spread, 0.0, 1.5) : 1.0;
        s.confidence = std::clamp(spread_quality, 0.0, 1.0);
        
        return s;
    }
    
    double getStress() const { return stress_; }
    void reset() { spread_ema_ = 0.0001; stress_ = 0; }
    
private:
    double spread_ema_, stress_;
};

// =============================================================================
// B4: Mean Reversion - REGIME CONDITIONAL
// =============================================================================
class MeanReversionStrategy {
public:
    MeanReversionStrategy() { reset(); }
    
    StrategySignal compute(const UnifiedTick& t, const MicrostructureSignals& /*sig*/, const RegimeState& regime) {
        double mid = 0.5 * (t.bid + t.ask);
        
        // VWAP proxy (slow EMA)
        vwap_ = 0.995 * vwap_ + 0.005 * mid;
        atr_ = 0.95 * atr_ + 0.05 * std::fabs(mid - last_mid_);
        last_mid_ = mid;
        
        double atr_safe = std::max(atr_, 1e-8);
        double deviation = (mid - vwap_) / atr_safe;
        
        // Reversion signal: negative deviation = buy
        double raw = -deviation;
        
        StrategySignal s;
        s.dir = (raw > 0.5) ? 1 : (raw < -0.5) ? -1 : 0;
        s.signal_abs = std::clamp(std::fabs(raw) / 1.2, 0.0, 1.0);
        
        // REGIME PENALTY: Mean reversion DIES in trends and volatility
        double regime_mult = 1.0;
        
        if (regime.is_trending) {
            regime_mult *= 0.2;  // Heavy penalty in trends
        }
        if (regime.is_volatile || regime.vol_z > 1.8) {
            regime_mult *= 0.3;  // Heavy penalty in vol expansion
        }
        if (regime.spread_z > 1.3) {
            regime_mult *= 0.6;  // Spread widening = stop hunts
        }
        
        s.confidence = std::clamp(regime_mult, 0.0, 1.0);
        
        return s;
    }
    
    void reset() { vwap_ = 0; atr_ = 0.0001; last_mid_ = 0; }
    
private:
    double vwap_, atr_, last_mid_;
};

// =============================================================================
// B5: Spread Regime - HARD VETO
// =============================================================================
class SpreadRegimeStrategy {
    static constexpr size_t WINDOW = 64;
    
public:
    SpreadRegimeStrategy() { reset(); }
    
    StrategySignal compute(const UnifiedTick& t, const MicrostructureSignals&, const RegimeState&) {
        // Track median spread
        spread_ema_ = 0.95 * spread_ema_ + 0.05 * t.spread;
        spread_z_ = (spread_ema_ > 0) ? (t.spread / spread_ema_) : 1.0;
        
        StrategySignal s;
        s.dir = 0;  // Spread doesn't tell direction
        s.signal_abs = std::clamp((spread_z_ - 1.0) / 1.5, 0.0, 1.0);
        s.confidence = 1.0;  // Always certain about spread state
        
        return s;
    }
    
    double getSpreadZ() const { return spread_z_; }
    bool shouldVeto(double threshold = 2.0) const { return spread_z_ > threshold; }
    void reset() { spread_ema_ = 0.0001; spread_z_ = 1.0; }
    
private:
    double spread_ema_, spread_z_;
};

// =============================================================================
// B6: Aggressor Burst - WITH REGIME AWARENESS
// =============================================================================
class AggressorBurstStrategy {
public:
    AggressorBurstStrategy() { reset(); }
    
    StrategySignal compute(const UnifiedTick& t, const MicrostructureSignals& sig, const RegimeState& regime) {
        double vol = t.buyVol + t.sellVol;
        vol_ema_ = 0.9 * vol_ema_ + 0.1 * vol;
        
        double burst_ratio = (vol_ema_ > 0) ? (vol / vol_ema_) : 1.0;
        double imbalance = sig.orderFlowImbalance;
        
        StrategySignal s;
        s.dir = (imbalance > 0.15) ? 1 : (imbalance < -0.15) ? -1 : 0;
        s.signal_abs = std::clamp(burst_ratio - 1.0, 0.0, 1.0);
        
        // Confidence: depth quality × regime quality
        double depth_factor = std::clamp(regime.liq_z, 0.2, 1.5);
        double regime_factor = 1.0;
        if (regime.spread_z > 1.5) regime_factor *= 0.7;
        if (regime.vol_z > 2.0) regime_factor *= 0.6;
        
        s.confidence = std::clamp(depth_factor * regime_factor, 0.0, 1.0);
        
        return s;
    }
    
    void reset() { vol_ema_ = 1.0; }
    
private:
    double vol_ema_;
};

// =============================================================================
// B7: Volatility State - RISK MODIFIER (Q_vol provider)
// =============================================================================
class VolatilityStrategy {
    static constexpr size_t FAST_WINDOW = 32;
    static constexpr size_t SLOW_WINDOW = 128;
    static constexpr double ALPHA_VOL = 2.0;
    
public:
    VolatilityStrategy() { reset(); }
    
    StrategySignal compute(const UnifiedTick& t, const MicrostructureSignals&, const RegimeState&) {
        double mid = 0.5 * (t.bid + t.ask);
        
        if (last_price_ > 0) {
            double r = mid - last_price_;
            fast_var_ = 0.9 * fast_var_ + 0.1 * r * r;
            slow_var_ = 0.99 * slow_var_ + 0.01 * r * r;
        }
        last_price_ = mid;
        
        fast_vol_ = std::sqrt(fast_var_);
        slow_vol_ = std::sqrt(slow_var_);
        
        vol_z_ = (slow_vol_ > 1e-10) ? (fast_vol_ / slow_vol_) : 1.0;
        Q_vol_ = 1.0 / (1.0 + ALPHA_VOL * std::max(0.0, vol_z_ - 1.0));
        
        StrategySignal s;
        s.dir = 0;  // Volatility doesn't determine direction
        s.signal_abs = std::clamp(vol_z_ - 1.0, 0.0, 1.0);
        s.confidence = 1.0;
        
        return s;
    }
    
    double getVolZ() const { return vol_z_; }
    double getQvol() const { return Q_vol_; }
    bool isVolatile() const { return vol_z_ > 1.5; }
    
    void reset() {
        last_price_ = 0; fast_var_ = 0; slow_var_ = 0;
        fast_vol_ = 0; slow_vol_ = 0; vol_z_ = 1.0; Q_vol_ = 1.0;
    }
    
private:
    double last_price_, fast_var_, slow_var_;
    double fast_vol_, slow_vol_, vol_z_, Q_vol_;
};

// =============================================================================
// B8: Latency Safety - HARD VETO
// =============================================================================
class LatencySafetyStrategy {
    static constexpr uint64_t VETO_NS = 150000;  // 150μs
    static constexpr double ALPHA_LAT = 3.0;
    
public:
    LatencySafetyStrategy() { reset(); }
    
    void updateLatency(uint64_t exec_latency_ns) {
        avg_lat_ = 0.9 * avg_lat_ + 0.1 * static_cast<double>(exec_latency_ns);
        baseline_ = 0.99 * baseline_ + 0.01 * static_cast<double>(exec_latency_ns);
    }
    
    StrategySignal compute(const UnifiedTick& /*t*/, const MicrostructureSignals&, const RegimeState&) {
        lat_z_ = (baseline_ > 0) ? (avg_lat_ / baseline_) : 1.0;
        Q_lat_ = 1.0 / (1.0 + ALPHA_LAT * std::max(0.0, lat_z_ - 1.0));
        
        StrategySignal s;
        s.dir = 0;
        s.signal_abs = std::clamp(lat_z_ - 1.0, 0.0, 1.0);
        s.confidence = 1.0;
        
        return s;
    }
    
    double getLatZ() const { return lat_z_; }
    double getQlat() const { return Q_lat_; }
    bool shouldVeto() const { return avg_lat_ > VETO_NS; }
    
    void reset() { avg_lat_ = 50000; baseline_ = 50000; lat_z_ = 1.0; Q_lat_ = 1.0; }
    
private:
    double avg_lat_, baseline_, lat_z_, Q_lat_;
};

// =============================================================================
// B9: Session Bias - RISK MODIFIER
// =============================================================================
class SessionBiasStrategy {
public:
    SessionBiasStrategy() { reset(); }
    
    StrategySignal compute(const UnifiedTick& /*t*/, const MicrostructureSignals&, const RegimeState& regime) {
        int hour = regime.utc_hour;
        
        // Default session weights
        if (hour >= 7 && hour <= 10) {
            session_mult_ = 1.2;  // London
        } else if (hour >= 13 && hour <= 16) {
            session_mult_ = 1.5;  // NY
        } else if (hour >= 21 || hour <= 2) {
            session_mult_ = 1.1;  // Asia
        } else {
            session_mult_ = 0.8;  // Off-hours
        }
        
        StrategySignal s;
        s.dir = 0;
        s.signal_abs = 1.0 - session_mult_ / 1.5;  // Higher mult = lower signal_abs
        s.confidence = 1.0;
        
        return s;
    }
    
    double getSessionMult() const { return session_mult_; }
    void reset() { session_mult_ = 1.0; }
    
private:
    double session_mult_;
};

// =============================================================================
// B11: Wyckoff Context Strategy - REGIME/CONTEXT MODIFIER
// =============================================================================
// NOT an indicator. NOT directional.
// Detects: Absorption, Effort vs Result, Range compression, False breaks
// Used to: Suppress momentum in ranges, allow reversion on springs
// Best for: XAUUSD, range-bound BTC, pre-breakout environments
// =============================================================================
class WyckoffContextStrategy {
    static constexpr double ABSORPTION_THRESHOLD = 0.6;
    static constexpr double FALSE_BREAK_VOLUME_Z = 1.5;
    
public:
    WyckoffContextStrategy() { reset(); }
    
    StrategySignal compute(const UnifiedTick& t, const MicrostructureSignals& /*sig*/, const RegimeState& /*regime*/) {
        double mid = 0.5 * (t.bid + t.ask);
        double volume = t.buyVol + t.sellVol;
        double price_change = mid - last_mid_;
        last_mid_ = mid;
        
        // === Update EMAs ===
        // Volume EMA for z-score
        vol_ema_ = 0.95 * vol_ema_ + 0.05 * volume;
        vol_var_ = 0.95 * vol_var_ + 0.05 * (volume - vol_ema_) * (volume - vol_ema_);
        double vol_std = std::sqrt(vol_var_ + 1e-10);
        double volume_z = (vol_std > 0) ? (volume - vol_ema_) / vol_std : 0.0;
        
        // ATR for expected move
        atr_fast_ = 0.9 * atr_fast_ + 0.1 * std::fabs(price_change);
        atr_slow_ = 0.99 * atr_slow_ + 0.01 * std::fabs(price_change);
        double atr = std::max(atr_fast_, 1e-10);
        double atr_long = std::max(atr_slow_, 1e-10);
        
        // Range tracking
        if (mid > range_high_ || range_high_ == 0) range_high_ = mid;
        if (mid < range_low_ || range_low_ > 1e9) range_low_ = mid;
        // Decay range towards current price slowly
        range_high_ = 0.9995 * range_high_ + 0.0005 * mid;
        range_low_ = 0.9995 * range_low_ + 0.0005 * mid;
        double range_width = range_high_ - range_low_;
        
        // === Core Wyckoff Mechanics ===
        
        // A) EFFORT VS RESULT (Wyckoff's core principle)
        // High volume with small price move = absorption
        double effort = std::max(0.0, volume_z);
        double result = std::fabs(price_change) / atr;
        double evr = effort / (result + 0.1);  // Effort vs Result ratio
        
        // B) ABSORPTION SCORE
        // High EVR = someone is absorbing (accumulation/distribution)
        absorption_ = 0.9 * absorption_ + 0.1 * std::clamp(evr / 3.0, 0.0, 1.0);
        
        // C) RANGE COMPRESSION
        // Tight range + activity = inventory building
        double compression = std::clamp(atr_long / (range_width + 1e-10), 0.0, 1.0);
        range_compression_ = 0.95 * range_compression_ + 0.05 * compression;
        
        // D) FALSE BREAK DETECTION (Spring/UTAD mechanics)
        // Broke range → rejected → high volume → back inside
        bool broke_high = (mid > range_high_ * 1.001);
        bool broke_low = (mid < range_low_ * 0.999);
        bool back_inside = (mid > range_low_ && mid < range_high_);
        
        if ((broke_high || broke_low) && volume_z > FALSE_BREAK_VOLUME_Z) {
            potential_false_break_ = true;
            false_break_side_ = broke_high ? 1 : -1;
        }
        
        if (potential_false_break_ && back_inside) {
            false_break_detected_ = true;
            false_break_count_++;
            potential_false_break_ = false;
        }
        
        // Decay false break flag
        if (false_break_detected_) {
            false_break_decay_++;
            if (false_break_decay_ > 20) {  // ~1 second at 50ms ticks
                false_break_detected_ = false;
                false_break_decay_ = 0;
            }
        }
        
        // === Build Signal ===
        StrategySignal s;
        s.dir = 0;  // CRITICAL: Non-directional - Wyckoff is CONTEXT
        
        // signal_abs = weighted combination of absorption + compression
        s.signal_abs = std::clamp(
            0.6 * absorption_ + 0.4 * range_compression_,
            0.0, 1.0
        );
        
        // Confidence boosted by false break detection
        s.confidence = false_break_detected_ ? 1.0 : 0.7;
        
        // Store for external access
        wyckoff_score_ = s.signal_abs;
        
        return s;
    }
    
    // === Accessors for risk system ===
    double getAbsorption() const { return absorption_; }
    double getRangeCompression() const { return range_compression_; }
    double getWyckoffScore() const { return wyckoff_score_; }
    bool isFalseBreak() const { return false_break_detected_; }
    int getFalseBreakSide() const { return false_break_side_; }  // 1=failed high, -1=failed low
    int getFalseBreakCount() const { return false_break_count_; }
    
    // === Suppression Logic ===
    // Returns how much to suppress momentum (0 = no suppress, 1 = full suppress)
    double getMomentumSuppression() const {
        // High absorption + compression = suppress momentum
        if (absorption_ > ABSORPTION_THRESHOLD && range_compression_ > 0.5) {
            return std::clamp(absorption_ * range_compression_ * 1.5, 0.0, 0.8);
        }
        return 0.0;
    }
    
    // Returns how much to boost reversion confidence (0 = no boost, 1 = full boost)  
    double getReversionBoost() const {
        // False break = encourage reversion
        if (false_break_detected_) {
            return 0.5;  // 50% confidence boost to reversion
        }
        // High absorption in range = mild reversion boost
        if (absorption_ > 0.5 && range_compression_ > 0.4) {
            return 0.2;
        }
        return 0.0;
    }
    
    void reset() {
        last_mid_ = 0;
        vol_ema_ = 1.0; vol_var_ = 1.0;
        atr_fast_ = 0.0001; atr_slow_ = 0.0001;
        range_high_ = 0; range_low_ = 1e10;
        absorption_ = 0; range_compression_ = 0;
        wyckoff_score_ = 0;
        potential_false_break_ = false;
        false_break_detected_ = false;
        false_break_side_ = 0;
        false_break_decay_ = 0;
        false_break_count_ = 0;
    }
    
private:
    double last_mid_;
    double vol_ema_, vol_var_;
    double atr_fast_, atr_slow_;
    double range_high_, range_low_;
    double absorption_, range_compression_;
    double wyckoff_score_;
    bool potential_false_break_;
    bool false_break_detected_;
    int false_break_side_;
    int false_break_decay_;
    int false_break_count_;
};

// =============================================================================
// B10: Confirmation Gate
// =============================================================================
class ConfirmationStrategy {
public:
    ConfirmationStrategy() { reset(); }
    
    StrategySignal compute(
        const std::array<StrategySignal, 9>& bucket_signals,
        const std::array<double, 10>& dir_weights
    ) {
        int agreeing_buy = 0, agreeing_sell = 0;
        double total_weight = 0;
        
        for (size_t i = 0; i < 9; ++i) {
            if (dir_weights[i] > 0 && bucket_signals[i].isActive()) {
                total_weight += dir_weights[i];
                if (bucket_signals[i].dir > 0) agreeing_buy++;
                else if (bucket_signals[i].dir < 0) agreeing_sell++;
            }
        }
        
        int max_directional = 5;  // B1, B2, B4, B6, B10
        int agreeing = std::max(agreeing_buy, agreeing_sell);
        
        StrategySignal s;
        s.dir = (agreeing_buy > agreeing_sell + 1) ? 1 : 
                (agreeing_sell > agreeing_buy + 1) ? -1 : 0;
        s.signal_abs = std::clamp((double)(agreeing - 1) / (max_directional - 1), 0.0, 1.0);
        s.confidence = std::clamp((double)agreeing / max_directional, 0.0, 1.0);
        
        return s;
    }
    
    void reset() {}
};

// =============================================================================
// Correlation Tracker - Penalizes B1/B2/B6 when they align too often
// =============================================================================
class CorrelationTracker {
    static constexpr size_t WINDOW = 256;
    static constexpr double LAMBDA = 0.7;
    static constexpr double P_MIN = 0.25;
    
public:
    CorrelationTracker() { reset(); }
    
    void update(double b1, double b2, double b6) {
        b1_[idx_] = b1; b2_[idx_] = b2; b6_[idx_] = b6;
        idx_ = (idx_ + 1) % WINDOW;
        if (count_ < WINDOW) count_++;
    }
    
    double computePenalty() const {
        if (count_ < 32) return 1.0;
        
        double r12 = corr(b1_, b2_);
        double r16 = corr(b1_, b6_);
        double r26 = corr(b2_, b6_);
        
        double avg = (std::fabs(r12) + std::fabs(r16) + std::fabs(r26)) / 3.0;
        return std::max(P_MIN, std::exp(-LAMBDA * avg * 3.0));
    }
    
    void reset() {
        std::fill(b1_.begin(), b1_.end(), 0.0);
        std::fill(b2_.begin(), b2_.end(), 0.0);
        std::fill(b6_.begin(), b6_.end(), 0.0);
        idx_ = 0; count_ = 0;
    }
    
private:
    double corr(const std::array<double, WINDOW>& a, const std::array<double, WINDOW>& b) const {
        double sA=0, sB=0, sAB=0, sA2=0, sB2=0;
        for (size_t i = 0; i < count_; ++i) {
            sA += a[i]; sB += b[i]; sAB += a[i]*b[i]; sA2 += a[i]*a[i]; sB2 += b[i]*b[i];
        }
        double num = count_ * sAB - sA * sB;
        double den = std::sqrt((count_ * sA2 - sA*sA) * (count_ * sB2 - sB*sB));
        return (den < 1e-10) ? 0.0 : num / den;
    }
    
    std::array<double, WINDOW> b1_, b2_, b6_;
    size_t idx_ = 0, count_ = 0;
};

// =============================================================================
// Aggregated Decision
// =============================================================================
struct BucketDecision {
    int8_t consensus = 0;          // -1, 0, +1
    double total_signal = 0.0;     // Weighted signal
    double avg_confidence = 0.0;
    double corr_penalty = 1.0;
    int buy_votes = 0;
    int sell_votes = 0;
    
    // Aliases for CfdEngine compatibility
    int buyVotes = 0;
    int sellVotes = 0;
    double avgConfidence = 0.0;
    double riskMultiplier = 1.0;
    
    bool vetoed = false;
    char veto_reason[32] = {0};
    
    // Quality factors for unified risk scaler
    double Q_vol = 1.0;
    double Q_spr = 1.0;
    double Q_liq = 1.0;
    double Q_lat = 1.0;
    
    // v6.72: RELAXED threshold - was 0.2
    bool shouldBuy() const { return !vetoed && consensus > 0 && avg_confidence > 0.1; }
    bool shouldSell() const { return !vetoed && consensus < 0 && avg_confidence > 0.1; }
    bool hasConsensus() const { return consensus != 0 && !vetoed; }
};

// =============================================================================
// Aggregator - For CfdEngine compatibility
// =============================================================================
class BucketAggregator {
public:
    void setWeights(const BucketWeights& w) { weights_ = w; }
    const BucketWeights& getWeights() const { return weights_; }
private:
    BucketWeights weights_;
};

// =============================================================================
// Strategy Pack - All 10 buckets + aggregation
// =============================================================================
class StrategyPack {
public:
    StrategyPack() : config_() {}
    
    // Aggregator for CfdEngine compatibility
    BucketAggregator aggregator;
    
    BucketDecision compute(const UnifiedTick& t, const MicrostructureSignals& sig) {
        // Update regime state
        regime_.vol_z = vol_.getVolZ();
        regime_.spread_z = spread_.getSpreadZ();
        regime_.liq_z = 1.0 - liq_.getStress();
        regime_.lat_z = latency_.getLatZ();
        regime_.is_volatile = vol_.isVolatile();
        regime_.is_trending = std::fabs(sig.trendStrength) > config_.trend_strength;
        regime_.utc_hour = static_cast<int>((t.tsLocal / 3600000000000ULL) % 24);
        
        // Compute all bucket signals
        std::array<StrategySignal, 10> signals;
        signals[0] = ofi_.compute(t, sig, regime_);
        signals[1] = momentum_.compute(t, sig, regime_);
        signals[2] = liq_.compute(t, sig, regime_);
        signals[3] = reversion_.compute(t, sig, regime_);
        signals[4] = spread_.compute(t, sig, regime_);
        signals[5] = aggressor_.compute(t, sig, regime_);
        signals[6] = vol_.compute(t, sig, regime_);
        signals[7] = latency_.compute(t, sig, regime_);
        signals[8] = session_.compute(t, sig, regime_);
        
        // =====================================================================
        // WYCKOFF CONTEXT - Modifies B2 (Momentum) and B4 (Reversion) confidence
        // =====================================================================
        wyckoff_.compute(t, sig, regime_);
        
        double mom_suppress = wyckoff_.getMomentumSuppression();  // [0, 0.8]
        double rev_boost = wyckoff_.getReversionBoost();          // [0, 0.5]
        
        // B2 Momentum: Suppress confidence in accumulation/distribution zones
        if (mom_suppress > 0.0) {
            signals[1].confidence *= (1.0 - mom_suppress);
        }
        
        // B4 Reversion: Boost confidence on springs/UTADs
        if (rev_boost > 0.0) {
            signals[3].confidence = std::min(1.0, signals[3].confidence * (1.0 + rev_boost));
        }
        // =====================================================================
        
        // B10 needs other signals
        std::array<StrategySignal, 9> first_nine;
        std::copy(signals.begin(), signals.begin() + 9, first_nine.begin());
        signals[9] = confirm_.compute(first_nine, config_.dir_weights);
        
        // Update correlation tracker
        corr_tracker_.update(signals[0].signal_abs * signals[0].dir,
                             signals[1].signal_abs * signals[1].dir,
                             signals[5].signal_abs * signals[5].dir);
        
        // Build decision
        BucketDecision d;
        d.corr_penalty = corr_tracker_.computePenalty();
        
        // HARD VETO CHECKS
        if (spread_.shouldVeto(config_.spread_veto_z)) {
            d.vetoed = true;
            snprintf(d.veto_reason, sizeof(d.veto_reason), "SPREAD:%.2f", spread_.getSpreadZ());
            return d;
        }
        if (latency_.shouldVeto()) {
            d.vetoed = true;
            snprintf(d.veto_reason, sizeof(d.veto_reason), "LATENCY:%.2f", latency_.getLatZ());
            return d;
        }
        
        // Quality factors
        d.Q_vol = vol_.getQvol();
        d.Q_spr = 1.0 / (1.0 + 2.0 * std::max(0.0, spread_.getSpreadZ() - 1.0));
        d.Q_liq = 1.0 - liq_.getStress() * 0.5;
        d.Q_lat = latency_.getQlat();
        
        // Aggregate directional signals
        double weighted_sum = 0, total_weight = 0, total_conf = 0;
        for (size_t i = 0; i < 10; ++i) {
            double w = config_.dir_weights[i];
            if (w > 0 && signals[i].isActive()) {
                // Apply correlation penalty to B1/B2/B6
                double adj_w = w;
                if (i == 0 || i == 1 || i == 5) {
                    adj_w *= d.corr_penalty;
                }
                
                weighted_sum += signals[i].dir * signals[i].signal_abs * adj_w;
                total_weight += adj_w;
                total_conf += signals[i].confidence;
                
                if (signals[i].dir > 0) d.buy_votes++;
                else if (signals[i].dir < 0) d.sell_votes++;
            }
        }
        
        if (total_weight > 0) {
            d.total_signal = weighted_sum / total_weight;
            int voting = d.buy_votes + d.sell_votes;
            d.avg_confidence = (voting > 0) ? total_conf / voting : 0;
        }
        
        // Set aliases for CfdEngine compatibility
        d.buyVotes = d.buy_votes;
        d.sellVotes = d.sell_votes;
        d.avgConfidence = d.avg_confidence;
        d.riskMultiplier = d.Q_vol * d.Q_spr * d.Q_liq * d.Q_lat * d.corr_penalty;
        
        // Consensus - v6.72 RELAXED: Just need any vote advantage
        // Was: majority AND > other + 1
        // Now: Just > other (any advantage triggers consensus)
        if (d.buy_votes > 0 && d.buy_votes > d.sell_votes) {
            d.consensus = 1;
        } else if (d.sell_votes > 0 && d.sell_votes > d.buy_votes) {
            d.consensus = -1;
        }
        
        return d;
    }
    
    void updateExecLatency(uint64_t ns) { latency_.updateLatency(ns); }
    
    void reset() {
        ofi_.reset(); momentum_.reset(); liq_.reset(); reversion_.reset();
        spread_.reset(); aggressor_.reset(); vol_.reset(); latency_.reset();
        session_.reset(); confirm_.reset(); corr_tracker_.reset(); wyckoff_.reset();
        regime_ = RegimeState{};
    }
    
    const RegimeState& getRegime() const { return regime_; }
    void setConfig(const BucketConfig& c) { config_ = c; }
    
    // Wyckoff accessors for diagnostics/GUI
    const WyckoffContextStrategy& getWyckoff() const { return wyckoff_; }
    
private:
    OFIStrategy ofi_;
    MomentumStrategy momentum_;
    LiquidityVacuumStrategy liq_;
    MeanReversionStrategy reversion_;
    SpreadRegimeStrategy spread_;
    AggressorBurstStrategy aggressor_;
    VolatilityStrategy vol_;
    LatencySafetyStrategy latency_;
    SessionBiasStrategy session_;
    ConfirmationStrategy confirm_;
    WyckoffContextStrategy wyckoff_;  // Context modifier, not a bucket
    
    CorrelationTracker corr_tracker_;
    RegimeState regime_;
    BucketConfig config_;
};

} // namespace Omega
