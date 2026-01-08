// =============================================================================
// MLModel.hpp - Chimera ML Gate Types (v4.6.0)
// =============================================================================
// PURPOSE: Types for ML Gate system (regime-specific quantile models)
// DESIGN: ML is a VETO + SIZE SCALER, not a signal generator
//
// NOTE: MLDecision struct is already defined in MLTypes.hpp
//       This file uses MLGateDecision enum for gate decisions
// =============================================================================
#pragma once

#include <cstdint>
#include <cstring>
#include <array>
#include <cmath>

namespace Chimera {
namespace ML {

// =============================================================================
// Regime Classification (for regime-separated models)
// =============================================================================
enum class Regime : uint8_t {
    TREND      = 0,   // Directional momentum
    MEANREV    = 1,   // Mean-reverting range
    BURST      = 2,   // High-volatility burst (gold pyramiding allowed here)
    DEAD       = 3    // Low liquidity / illiquid
};

inline const char* regimeToStr(Regime r) {
    switch (r) {
        case Regime::TREND:   return "TREND";
        case Regime::MEANREV: return "MEANREV";
        case Regime::BURST:   return "BURST";
        case Regime::DEAD:    return "DEAD";
        default: return "UNKNOWN";
    }
}

// =============================================================================
// Session Classification (for session-aware thresholds)
// =============================================================================
enum class Session : uint8_t {
    ASIA   = 0,   // 21:00-07:00 UTC - Very selective
    LONDON = 1,   // 07:00-12:30 UTC - Moderate
    NY     = 2    // 12:30-21:00 UTC - Fastest + deepest liquidity
};

inline const char* sessionToStr(Session s) {
    switch (s) {
        case Session::ASIA:   return "ASIA";
        case Session::LONDON: return "LONDON";
        case Session::NY:     return "NY";
        default: return "UNKNOWN";
    }
}

inline Session getSessionFromUTCHour(int utc_hour) {
    // ASIA: 21:00-07:00 UTC
    if (utc_hour >= 21 || utc_hour < 7) return Session::ASIA;
    // LONDON: 07:00-12:30 UTC (we use 13 as cutoff)
    if (utc_hour >= 7 && utc_hour < 13) return Session::LONDON;
    // NY: 12:30-21:00 UTC
    return Session::NY;
}

// =============================================================================
// ML Quantile Output (from regime-specific models)
// =============================================================================
// Models predict realized PnL distribution, not direction
// =============================================================================
struct MLQuantiles {
    double q10 = 0.0;   // 10th percentile (downside tail)
    double q25 = 0.0;   // 25th percentile
    double q50 = 0.0;   // Median (central expectancy)
    double q75 = 0.0;   // 75th percentile
    double q90 = 0.0;   // 90th percentile (upside tail)
    
    // Derived metrics
    double iqr() const { return q75 - q25; }                    // Interquartile range
    double upside_skew() const { return q90 - q50; }            // Upside potential
    double downside_risk() const { return q50 - q10; }          // Downside risk
    double asymmetry() const { 
        double down = q50 - q10;
        return down > 0.0001 ? (q90 - q50) / down : 0.0; 
    }
    
    // Confidence metric: (q75 - q25) / abs(q50)
    // Clamped denominator to avoid NaN/explosion when q50 ≈ 0
    double confidence() const {
        double abs_q50 = std::max(std::fabs(q50), 0.05);  // Min 0.05 to avoid explosion
        return iqr() / abs_q50;
    }
    
    // Tail spread: (q50 - q10) - useful for detecting regime stress
    double tailSpread() const { return q50 - q10; }
    
    // Risk checks
    bool hasAsymmetricUpside(double min_ratio = 1.5) const {
        return asymmetry() >= min_ratio;
    }
    
    bool hasFatTail(double max_tail = -2.0) const {
        return q10 < max_tail;
    }
    
    // ML Health scalar: single glance system status
    // Combines expectancy (q50) with confidence
    // Range: [-5, +5], higher is better
    double health() const {
        return std::clamp(q50 * confidence(), -5.0, 5.0);
    }
};

// =============================================================================
// Session-Specific Thresholds (v4.6.0 - FROZEN)
// =============================================================================
// DO NOT MODIFY without version bump - protects backtests
// =============================================================================
struct SessionThresholds_v460 {
    double min_edge;          // Minimum q50 required
    double max_tail_loss;     // Maximum acceptable q10 (ABSOLUTE)
    double max_latency_us;    // Maximum latency before blocking
    double min_iqr;           // Minimum IQR to ensure distribution is meaningful
    double max_size_scale;    // Session-specific size cap
    double tail_spread_max;   // Max (q50 - q10) RELATIVE threshold
    
    static SessionThresholds_v460 forSession(Session s) {
        switch (s) {
            case Session::ASIA:
                // Very selective - thin markets, max size 0.6
                return { 1.8, 1.2, 120.0, 0.5, 0.6, 2.5 };
            case Session::LONDON:
                // Moderate selectivity, max size 1.0
                return { 1.3, 1.5, 180.0, 0.4, 1.0, 3.0 };
            case Session::NY:
                // Most permissive - fastest + deepest, max size 1.5
                return { 1.0, 2.0, 250.0, 0.3, 1.5, 3.5 };
        }
        return { 2.0, 1.0, 100.0, 0.5, 0.5, 2.0 };  // Conservative default
    }
};

// Alias for current version
using SessionThresholds = SessionThresholds_v460;

// =============================================================================
// ML Gate Decision (renamed to avoid conflict with MLTypes.hpp MLDecision)
// =============================================================================
enum class MLGateDecision : uint8_t {
    REJECT = 0,
    ACCEPT = 1
};

inline const char* gateDecisionToStr(MLGateDecision d) {
    return d == MLGateDecision::ACCEPT ? "ACCEPT" : "REJECT";
}

// =============================================================================
// Reject Reason Codes (for attribution logging)
// =============================================================================
enum class RejectReason : uint8_t {
    NONE           = 0,   // Not rejected (accepted)
    IQR_TOO_NARROW = 1,
    TAIL_RISK_HIGH = 2,
    EDGE_LOW       = 3,
    LATENCY_HIGH   = 4,
    DEAD_REGIME    = 5,
    DRIFT_KILL     = 6,
    DRIFT_THROTTLE = 7,
    SESSION_BLOCK  = 8,
    TAIL_SPREAD    = 9,   // Tail spread too wide relative to q50
    ML_DISABLED    = 10   // ML bypassed (symbol disabled or warmup)
};

inline const char* rejectReasonToStr(RejectReason r) {
    switch (r) {
        case RejectReason::NONE:           return "NONE";
        case RejectReason::IQR_TOO_NARROW: return "IQR_TOO_NARROW";
        case RejectReason::TAIL_RISK_HIGH: return "TAIL_RISK_HIGH";
        case RejectReason::EDGE_LOW:       return "EDGE_LOW";
        case RejectReason::LATENCY_HIGH:   return "LATENCY_HIGH";
        case RejectReason::DEAD_REGIME:    return "DEAD_REGIME";
        case RejectReason::DRIFT_KILL:     return "DRIFT_KILL";
        case RejectReason::DRIFT_THROTTLE: return "DRIFT_THROTTLE";
        case RejectReason::SESSION_BLOCK:  return "SESSION_BLOCK";
        case RejectReason::TAIL_SPREAD:    return "TAIL_SPREAD";
        case RejectReason::ML_DISABLED:    return "ML_DISABLED";
        default: return "UNKNOWN";
    }
}

// =============================================================================
// ML Gate Result (full decision context)
// =============================================================================
struct MLGateResult {
    MLGateDecision decision = MLGateDecision::REJECT;
    double size_scale = 1.0;      // 0.25 to session max
    RejectReason reject_reason = RejectReason::NONE;
    
    // Context
    MLQuantiles quantiles;
    Regime regime = Regime::DEAD;
    Session session = Session::ASIA;
    double latency_us = 0.0;
    double latency_penalty = 0.0;
    double required_edge = 0.0;
    double confidence = 0.0;      // ML confidence metric
    
    bool accepted() const { return decision == MLGateDecision::ACCEPT; }
    
    // =======================================================================
    // Create a "bypass" result when ML is disabled
    // IMPORTANT: Use this when symbol ML is disabled or during warmup
    // Returns ACCEPT with size_scale=1.0, properly initialized
    // =======================================================================
    static MLGateResult bypass(Regime r = Regime::DEAD, Session s = Session::NY) {
        MLGateResult result;
        result.decision = MLGateDecision::ACCEPT;  // Allow trade
        result.size_scale = 1.0;                   // Base size (no adjustment)
        result.reject_reason = RejectReason::ML_DISABLED;
        result.regime = r;
        result.session = s;
        result.latency_us = 0.0;
        result.latency_penalty = 0.0;
        result.required_edge = 0.0;
        result.confidence = 0.0;
        // quantiles left at default (zeros) - do NOT log to attribution
        return result;
    }
};

// =============================================================================
// Symbol-specific model configuration
// =============================================================================
struct SymbolMLConfig {
    char symbol[16] = {0};
    bool ml_enabled = false;
    bool pyramiding_allowed = false;  // Only XAUUSD in BURST
    double base_edge = 1.0;
    double max_tail_loss = 2.0;
    
    // Feature indices (after SHAP pruning)
    std::array<uint8_t, 32> feature_indices = {0};
    uint8_t num_features = 0;
};

} // namespace ML
} // namespace Chimera
