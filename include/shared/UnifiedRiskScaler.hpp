// =============================================================================
// UnifiedRiskScaler.hpp - Single Source of Truth for Risk
// =============================================================================
// FORMULA:
//   size = BaseRisk × E × R
//   E = |S| × Conf  (edge factor)
//   R = Health × Sess × CorrP × Q_vol × Q_spr × Q_liq × Q_lat × Q_dd
//
// This replaces binary allow/deny with smooth, explainable risk decay.
// =============================================================================
#pragma once

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <array>

namespace Chimera {

// =============================================================================
// Risk Inputs - All factors that affect position sizing
// =============================================================================
struct RiskInputs {
    // Edge factors
    double signal_abs = 0.0;      // |S| in [0,1] - signal strength
    double confidence = 0.0;      // Conf in [0,1] - signal reliability
    
    // Quality z-scores (1.0 = normal, >1.0 = degraded)
    double vol_z = 1.0;           // fast_vol / slow_vol
    double spread_z = 1.0;        // spread / median_spread
    double liquidity_z = 1.0;     // depth / median_depth (higher = better)
    double latency_z = 1.0;       // latency / baseline_latency
    
    // Penalties and weights
    double correlation_penalty = 1.0;  // CorrP in [0.25, 1]
    double session_weight = 1.0;       // Sess in [0, 1]
    double health = 1.0;               // Feed health in [0, 1]
    double drawdown_used = 0.0;        // |DD_current| / |DD_max| in [0, 1]
};

// =============================================================================
// Risk Parameters - Per-instrument tuning
// =============================================================================
struct RiskParams {
    double base_risk = 0.0005;    // BaseRisk (fraction of equity)
    
    // Suppressor alphas (higher = more aggressive suppression)
    double alpha_vol = 1.5;       // Volatility suppressor
    double alpha_spread = 2.0;    // Spread suppressor
    double beta_liquidity = 0.8;  // Liquidity gate (multiplier)
    double alpha_latency = 3.0;   // Latency suppressor
    
    double dd_exponent = 2.0;     // Drawdown throttle curvature
    
    // Hard limits
    double max_size = 0.01;       // Maximum position size (1% equity)
    double min_R = 0.05;          // Minimum R to allow trading
};

// =============================================================================
// Risk Output - Final decision
// =============================================================================
struct RiskOutput {
    double size = 0.0;            // Final position fraction
    double R = 0.0;               // Total risk multiplier [0,1]
    double E = 0.0;               // Edge factor
    
    // Individual quality factors (for GUI display)
    double Q_vol = 1.0;
    double Q_spr = 1.0;
    double Q_liq = 1.0;
    double Q_lat = 1.0;
    double Q_dd = 1.0;
    
    bool allow = false;           // Final allow/deny
    uint32_t suppress_mask = 0;   // Which factors suppressed (for debugging)
};

// =============================================================================
// Suppression Reasons (bitmask)
// =============================================================================
enum SuppressReason : uint32_t {
    SUPPRESS_NONE       = 0,
    SUPPRESS_HEALTH     = 1 << 0,
    SUPPRESS_SESSION    = 1 << 1,
    SUPPRESS_CORR       = 1 << 2,
    SUPPRESS_VOL        = 1 << 3,
    SUPPRESS_SPREAD     = 1 << 4,
    SUPPRESS_LIQUIDITY  = 1 << 5,
    SUPPRESS_LATENCY    = 1 << 6,
    SUPPRESS_DRAWDOWN   = 1 << 7,
    SUPPRESS_EDGE       = 1 << 8
};

// =============================================================================
// Unified Risk Scaler
// =============================================================================
class UnifiedRiskScaler {
public:
    UnifiedRiskScaler() = default;
    explicit UnifiedRiskScaler(const RiskParams& params) : params_(params) {}
    
    void setParams(const RiskParams& p) { params_ = p; }
    const RiskParams& getParams() const { return params_; }
    
    // =============================================================================
    // Main computation - THE FORMULA
    // =============================================================================
    RiskOutput compute(const RiskInputs& in) const {
        RiskOutput out;
        out.suppress_mask = SUPPRESS_NONE;
        
        // --- Edge Factor ---
        // E = |S| × Conf
        out.E = std::clamp(in.signal_abs, 0.0, 1.0) 
              * std::clamp(in.confidence, 0.0, 1.0);
        
        if (out.E < 0.01) {
            out.suppress_mask |= SUPPRESS_EDGE;
        }
        
        // --- Quality Suppressors ---
        
        // Q_vol = 1 / (1 + α_v × max(0, VolZ - 1))
        out.Q_vol = 1.0 / (1.0 + params_.alpha_vol * std::max(0.0, in.vol_z - 1.0));
        if (out.Q_vol < 0.5) out.suppress_mask |= SUPPRESS_VOL;
        
        // Q_spr = 1 / (1 + α_s × max(0, SprZ - 1))
        out.Q_spr = 1.0 / (1.0 + params_.alpha_spread * std::max(0.0, in.spread_z - 1.0));
        if (out.Q_spr < 0.5) out.suppress_mask |= SUPPRESS_SPREAD;
        
        // Q_liq = clamp(β_l × LiqZ, 0, 1)
        out.Q_liq = std::clamp(params_.beta_liquidity * in.liquidity_z, 0.0, 1.0);
        if (out.Q_liq < 0.5) out.suppress_mask |= SUPPRESS_LIQUIDITY;
        
        // Q_lat = 1 / (1 + α_l × max(0, LatZ - 1))
        out.Q_lat = 1.0 / (1.0 + params_.alpha_latency * std::max(0.0, in.latency_z - 1.0));
        if (out.Q_lat < 0.5) out.suppress_mask |= SUPPRESS_LATENCY;
        
        // Q_dd = clamp(1 - DD_used^exp, 0, 1)
        out.Q_dd = std::clamp(1.0 - std::pow(in.drawdown_used, params_.dd_exponent), 0.0, 1.0);
        if (out.Q_dd < 0.5) out.suppress_mask |= SUPPRESS_DRAWDOWN;
        
        // --- Penalties ---
        double health_factor = std::clamp(in.health, 0.0, 1.0);
        if (health_factor < 0.5) out.suppress_mask |= SUPPRESS_HEALTH;
        
        double session_factor = std::clamp(in.session_weight, 0.0, 1.0);
        if (session_factor < 0.5) out.suppress_mask |= SUPPRESS_SESSION;
        
        double corr_factor = std::clamp(in.correlation_penalty, 0.0, 1.0);
        if (corr_factor < 0.5) out.suppress_mask |= SUPPRESS_CORR;
        
        // --- Total Risk Multiplier ---
        // R = Health × Sess × CorrP × Q_vol × Q_spr × Q_liq × Q_lat × Q_dd
        out.R = health_factor 
              * session_factor 
              * corr_factor 
              * out.Q_vol 
              * out.Q_spr 
              * out.Q_liq 
              * out.Q_lat 
              * out.Q_dd;
        
        out.R = std::clamp(out.R, 0.0, 1.0);
        
        // --- Final Position Size ---
        // size = BaseRisk × E × R
        out.size = params_.base_risk * out.E * out.R;
        out.size = std::clamp(out.size, 0.0, params_.max_size);
        
        // --- Allow/Deny ---
        out.allow = (out.R >= params_.min_R) && (out.size > 0.0);
        
        return out;
    }
    
    // =============================================================================
    // Debug: Get suppression reason string
    // =============================================================================
    static const char* suppressReasonString(uint32_t mask) {
        if (mask & SUPPRESS_HEALTH) return "HEALTH";
        if (mask & SUPPRESS_SESSION) return "SESSION";
        if (mask & SUPPRESS_CORR) return "CORRELATION";
        if (mask & SUPPRESS_VOL) return "VOLATILITY";
        if (mask & SUPPRESS_SPREAD) return "SPREAD";
        if (mask & SUPPRESS_LIQUIDITY) return "LIQUIDITY";
        if (mask & SUPPRESS_LATENCY) return "LATENCY";
        if (mask & SUPPRESS_DRAWDOWN) return "DRAWDOWN";
        if (mask & SUPPRESS_EDGE) return "EDGE";
        return "NONE";
    }
    
private:
    RiskParams params_;
};

// =============================================================================
// Pre-defined Instrument Profiles
// v4.11.0: CRYPTO REMOVED - CFD only
// =============================================================================
namespace InstrumentProfiles {

// XAUUSD - Gold CFD, session-sensitive, spread-critical
inline RiskParams XAUUSD() {
    RiskParams p;
    p.base_risk = 0.0010;      // 0.10%
    p.alpha_vol = 1.2;
    p.alpha_spread = 2.0;
    p.beta_liquidity = 1.0;
    p.alpha_latency = 0.5;
    p.dd_exponent = 2.2;
    p.max_size = 0.008;
    p.min_R = 0.05;
    return p;
}

// NAS100 - Index CFD, trend-dominant, session-critical
inline RiskParams NAS100() {
    RiskParams p;
    p.base_risk = 0.0013;      // 0.13%
    p.alpha_vol = 0.9;
    p.alpha_spread = 1.5;
    p.beta_liquidity = 1.1;
    p.alpha_latency = 0.5;
    p.dd_exponent = 1.8;
    p.max_size = 0.010;
    p.min_R = 0.05;
    return p;
}

// EURUSD - Major forex, most liquid
inline RiskParams EURUSD() {
    RiskParams p;
    p.base_risk = 0.0015;
    p.alpha_vol = 1.0;
    p.alpha_spread = 1.2;
    p.beta_liquidity = 1.2;
    p.alpha_latency = 0.3;
    p.dd_exponent = 2.0;
    p.max_size = 0.012;
    p.min_R = 0.05;
    return p;
}

} // namespace InstrumentProfiles

// =============================================================================
// COMPLETE Instrument Profiles for ALL Trading Symbols
// v4.11.0: CRYPTO REMOVED - CFD only
// =============================================================================
namespace AllProfiles {

// --- FOREX MAJORS ---
inline RiskParams EURUSD() { return InstrumentProfiles::EURUSD(); }

inline RiskParams GBPUSD() {
    RiskParams p;
    p.base_risk = 0.0012;
    p.alpha_vol = 1.1;
    p.alpha_spread = 1.3;
    p.beta_liquidity = 1.1;
    p.alpha_latency = 0.3;
    p.dd_exponent = 2.0;
    p.max_size = 0.010;
    p.min_R = 0.05;
    return p;
}

inline RiskParams USDJPY() {
    RiskParams p;
    p.base_risk = 0.0012;
    p.alpha_vol = 1.0;
    p.alpha_spread = 1.2;
    p.beta_liquidity = 1.2;
    p.alpha_latency = 0.3;
    p.dd_exponent = 2.0;
    p.max_size = 0.010;
    p.min_R = 0.05;
    return p;
}

inline RiskParams AUDUSD() {
    RiskParams p;
    p.base_risk = 0.0010;
    p.alpha_vol = 1.2;
    p.alpha_spread = 1.4;
    p.beta_liquidity = 1.0;
    p.alpha_latency = 0.3;
    p.dd_exponent = 2.0;
    p.max_size = 0.008;
    p.min_R = 0.05;
    return p;
}

inline RiskParams USDCAD() {
    RiskParams p;
    p.base_risk = 0.0010;
    p.alpha_vol = 1.1;
    p.alpha_spread = 1.3;
    p.beta_liquidity = 1.0;
    p.alpha_latency = 0.3;
    p.dd_exponent = 2.0;
    p.max_size = 0.008;
    p.min_R = 0.05;
    return p;
}

inline RiskParams AUDNZD() {
    RiskParams p;
    p.base_risk = 0.0008;
    p.alpha_vol = 1.3;
    p.alpha_spread = 1.6;
    p.beta_liquidity = 0.9;
    p.alpha_latency = 0.3;
    p.dd_exponent = 2.0;
    p.max_size = 0.006;
    p.min_R = 0.05;
    return p;
}

inline RiskParams USDCHF() {
    RiskParams p;
    p.base_risk = 0.0010;
    p.alpha_vol = 1.0;
    p.alpha_spread = 1.3;
    p.beta_liquidity = 1.0;
    p.alpha_latency = 0.3;
    p.dd_exponent = 2.0;
    p.max_size = 0.008;
    p.min_R = 0.05;
    return p;
}

// --- METALS ---
inline RiskParams XAUUSD() { return InstrumentProfiles::XAUUSD(); }

inline RiskParams XAGUSD() {
    RiskParams p;
    p.base_risk = 0.0008;
    p.alpha_vol = 1.5;        // Silver more volatile than gold
    p.alpha_spread = 2.2;
    p.beta_liquidity = 0.8;
    p.alpha_latency = 0.5;
    p.dd_exponent = 2.2;
    p.max_size = 0.006;
    p.min_R = 0.05;
    return p;
}

// --- INDICES ---
inline RiskParams NAS100() { return InstrumentProfiles::NAS100(); }

inline RiskParams SPX500() {
    RiskParams p;
    p.base_risk = 0.0012;
    p.alpha_vol = 0.8;
    p.alpha_spread = 1.4;
    p.beta_liquidity = 1.2;
    p.alpha_latency = 0.5;
    p.dd_exponent = 1.8;
    p.max_size = 0.010;
    p.min_R = 0.05;
    return p;
}

inline RiskParams US30() {
    RiskParams p;
    p.base_risk = 0.0012;
    p.alpha_vol = 0.9;
    p.alpha_spread = 1.5;
    p.beta_liquidity = 1.1;
    p.alpha_latency = 0.5;
    p.dd_exponent = 1.8;
    p.max_size = 0.010;
    p.min_R = 0.05;
    return p;
}

// --- DEFAULT PROFILE (for any unlisted symbol) ---
inline RiskParams DEFAULT() {
    RiskParams p;
    p.base_risk = 0.0008;
    p.alpha_vol = 1.0;
    p.alpha_spread = 1.5;
    p.beta_liquidity = 1.0;
    p.alpha_latency = 0.3;     // Relaxed
    p.dd_exponent = 2.0;
    p.max_size = 0.005;
    p.min_R = 0.05;            // Relaxed
    return p;
}

// Lookup function - returns appropriate profile for any symbol
// v4.11.0: crypto removed
inline RiskParams getProfile(const char* symbol) {
    if (strcmp(symbol, "EURUSD") == 0) return EURUSD();
    if (strcmp(symbol, "GBPUSD") == 0) return GBPUSD();
    if (strcmp(symbol, "USDJPY") == 0) return USDJPY();
    if (strcmp(symbol, "AUDUSD") == 0) return AUDUSD();
    if (strcmp(symbol, "USDCAD") == 0) return USDCAD();
    if (strcmp(symbol, "AUDNZD") == 0) return AUDNZD();
    if (strcmp(symbol, "USDCHF") == 0) return USDCHF();
    if (strcmp(symbol, "XAUUSD") == 0) return XAUUSD();
    if (strcmp(symbol, "XAGUSD") == 0) return XAGUSD();
    if (strcmp(symbol, "NAS100") == 0) return NAS100();
    if (strcmp(symbol, "SPX500") == 0) return SPX500();
    if (strcmp(symbol, "US30") == 0) return US30();
    return DEFAULT();
}

} // namespace AllProfiles

// =============================================================================
// Session Weights
// v4.11.0: crypto removed
// =============================================================================
namespace SessionWeights {

// Returns session weight based on UTC hour
inline double getSessionWeight(int utc_hour, const char* instrument) {
    // XAUUSD - London/NY only
    if (strcmp(instrument, "XAUUSD") == 0) {
        if (utc_hour >= 7 && utc_hour <= 10) return 1.0;   // London
        if (utc_hour >= 13 && utc_hour <= 16) return 1.0;  // NY
        if (utc_hour >= 21 || utc_hour <= 2) return 0.3;   // Asia (reduced)
        return 0.0;  // Off-hours - NO TRADING
    }
    
    // NAS100 - NY session only
    if (strcmp(instrument, "NAS100") == 0 || strcmp(instrument, "US30") == 0) {
        if (utc_hour >= 13 && utc_hour <= 20) return 1.0;  // NY RTH
        if (utc_hour >= 10 && utc_hour < 13) return 0.6;   // Pre-NY
        if (utc_hour >= 20 && utc_hour <= 22) return 0.4;  // Post-close
        return 0.0;  // Asia - NO TRADING
    }
    
    // Default forex
    if (utc_hour >= 7 && utc_hour <= 10) return 1.2;   // London
    if (utc_hour >= 13 && utc_hour <= 16) return 1.5;  // NY
    if (utc_hour >= 21 || utc_hour <= 2) return 1.1;   // Asia
    return 0.8;
}

} // namespace SessionWeights

} // namespace Chimera
