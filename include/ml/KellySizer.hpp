// =============================================================================
// KellySizer.hpp - Capital-Scaled Kelly Dampening with Regime Awareness
// =============================================================================
// PURPOSE: Compute position size using Kelly criterion with safety dampeners
// DESIGN:
//   - Raw Kelly from ML predictions (prob_win, expected_R)
//   - Three-layer dampening: capital, drawdown, regime
//   - Hard caps to prevent ruin
//   - Regime-specific curves (LOW_VOL aggressive, CRISIS conservative)
//
// MATH:
//   f* = (p * b - (1 - p)) / b
//   where:
//     p = probability of win
//     b = win/loss ratio = expected_R / |avg_loss_R|
//   
//   Final fraction = f* × capital_damp × drawdown_damp × regime_mult
//
// WHY KELLY:
//   - Maximizes geometric growth rate
//   - Accounts for edge AND variance
//   - Well-understood theoretical properties
//
// WHY DAMPEN:
//   - Raw Kelly is too aggressive for real markets
//   - Estimation error in probabilities
//   - Tail risks not captured by model
//   - Transaction costs and slippage
//
// USAGE:
//   KellySizer sizer;
//   KellyInputs inputs{prob_win, expected_R, avg_loss_R, equity, dd_pct, regime_mult};
//   double frac = sizer.computeFraction(inputs);
//   double size = equity * frac * base_size_multiplier;
// =============================================================================
#pragma once

#include "MLTypes.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <string>

namespace Chimera {
namespace ML {

// =============================================================================
// Kelly Curve - Regime-specific scaling
// =============================================================================
struct KellyCurve {
    double scale;   // Multiplier on raw Kelly (e.g., 0.7 = 70% of Kelly)
    double cap;     // Hard cap on fraction (e.g., 0.07 = 7% max)
    
    KellyCurve() noexcept : scale(1.0), cap(0.10) {}
    KellyCurve(double s, double c) noexcept : scale(s), cap(c) {}
};

// =============================================================================
// Kelly Sizer Configuration
// =============================================================================
struct KellyConfig {
    // Global caps
    double max_kelly_raw = 0.25;       // Never exceed 25% raw Kelly
    double max_kelly_final = 0.10;     // Never exceed 10% final fraction
    double min_fraction = 0.0;         // Allow skip (0%)
    
    // Capital dampening (log-scale)
    double capital_log_base = 10.0;    // log1p(equity) / this
    
    // Drawdown dampening
    double max_drawdown_pct = 0.25;    // At 25% DD, dampening maxes out
    double max_dd_dampen = 0.70;       // Max reduction at max DD (30% left)
    
    // Edge thresholds
    double min_edge = 0.02;            // Don't trade if edge < 2%
    double min_prob = 0.40;            // Don't trade if P(win) < 40%
    
    // Half-Kelly default (industry standard)
    double default_fractional_kelly = 0.5;
};

// =============================================================================
// Kelly Sizer
// =============================================================================
class KellySizer {
public:
    KellySizer() noexcept : KellySizer(KellyConfig()) {}
    
    explicit KellySizer(const KellyConfig& config) noexcept : config_(config) {
        // Default regime curves
        curves_[static_cast<int>(MLRegime::LOW_VOL)]    = KellyCurve(1.20, 0.12);
        curves_[static_cast<int>(MLRegime::NORMAL_VOL)] = KellyCurve(1.00, 0.10);
        curves_[static_cast<int>(MLRegime::HIGH_VOL)]   = KellyCurve(0.70, 0.07);
        curves_[static_cast<int>(MLRegime::CRISIS)]     = KellyCurve(0.30, 0.03);
    }
    
    // =========================================================================
    // Core Computation
    // =========================================================================
    
    // Compute position fraction given inputs and regime
    double computeFraction(const KellyInputs& in, MLRegime regime) noexcept {
        // Sanity checks
        if (in.prob_win < config_.min_prob || in.prob_win > 1.0) {
            return 0.0;  // Don't trade
        }
        if (std::fabs(in.avg_loss_R) < 1e-9) {
            return 0.0;  // No loss data
        }
        
        // Compute b (win/loss ratio)
        double b = in.expected_R / std::fabs(in.avg_loss_R);
        if (b <= 0.0) {
            return 0.0;  // No edge
        }
        
        // Raw Kelly
        double kelly = (in.prob_win * b - (1.0 - in.prob_win)) / b;
        
        // Check edge threshold
        if (kelly < config_.min_edge) {
            return 0.0;  // Edge too small
        }
        
        // Apply fractional Kelly (typically half Kelly)
        kelly *= config_.default_fractional_kelly;
        
        // Cap raw Kelly
        kelly = std::clamp(kelly, 0.0, config_.max_kelly_raw);
        
        // =====================================================================
        // DAMPENER 1: Capital (log-scale)
        // Larger accounts size more conservatively relative to account
        // =====================================================================
        double capital_damp = 1.0;
        if (in.equity > 0) {
            capital_damp = std::log1p(in.equity) / config_.capital_log_base;
            capital_damp = std::clamp(capital_damp, 0.5, 2.0);
        }
        
        // =====================================================================
        // DAMPENER 2: Drawdown
        // Reduce size in drawdown to preserve capital
        // =====================================================================
        double dd_damp = 1.0;
        if (in.drawdown_pct > 0) {
            double dd_ratio = in.drawdown_pct / config_.max_drawdown_pct;
            dd_ratio = std::clamp(dd_ratio, 0.0, 1.0);
            dd_damp = 1.0 - (dd_ratio * config_.max_dd_dampen);
        }
        
        // =====================================================================
        // DAMPENER 3: Regime
        // Crisis = conservative, low vol = aggressive
        // =====================================================================
        KellyCurve curve = getCurve(regime);
        double regime_mult = curve.scale * in.regime_mult;
        
        // Final computation
        double final_frac = kelly * capital_damp * dd_damp * regime_mult;
        
        // Apply regime cap and global cap
        final_frac = std::min(final_frac, curve.cap);
        final_frac = std::clamp(final_frac, config_.min_fraction, config_.max_kelly_final);
        
        return final_frac;
    }
    
    // Convenience: use inputs' regime_mult as regime selector
    double computeFraction(const KellyInputs& in) noexcept {
        // Determine regime from inputs (or default to NORMAL)
        return computeFraction(in, MLRegime::NORMAL_VOL);
    }
    
    // =========================================================================
    // From ML Decision
    // =========================================================================
    
    double computeFromML(const MLDecision& ml, double equity, 
                         double drawdown_pct, double avg_loss_R) noexcept {
        if (!ml.ml_active || !ml.allow_trade) {
            return 0.0;
        }
        
        KellyInputs in;
        in.prob_win = ml.prob_positive;
        in.expected_R = ml.expected_R;
        in.avg_loss_R = avg_loss_R;
        in.equity = equity;
        in.drawdown_pct = drawdown_pct;
        in.regime_mult = 1.0;
        
        return computeFraction(in, ml.regime_used);
    }
    
    // =========================================================================
    // Regime Curve Management
    // =========================================================================
    
    void setCurve(MLRegime regime, double scale, double cap) noexcept {
        int idx = static_cast<int>(regime);
        if (idx >= 0 && idx < 4) {
            curves_[idx] = KellyCurve(scale, cap);
        }
    }
    
    KellyCurve getCurve(MLRegime regime) const noexcept {
        int idx = static_cast<int>(regime);
        if (idx >= 0 && idx < 4) {
            return curves_[idx];
        }
        return curves_[1];  // Default NORMAL_VOL
    }
    
    // Load curves from CSV file
    bool loadCurves(const char* path) noexcept {
        std::ifstream f(path);
        if (!f.is_open()) {
            std::fprintf(stderr, "[KellySizer] Cannot open %s\n", path);
            return false;
        }
        
        std::string line;
        std::getline(f, line);  // Skip header
        
        while (std::getline(f, line)) {
            std::stringstream ss(line);
            std::string regime_str;
            double scale, cap;
            char comma;
            
            ss >> regime_str >> comma >> scale >> comma >> cap;
            
            MLRegime regime;
            if (regime_str == "LOW_VOL") regime = MLRegime::LOW_VOL;
            else if (regime_str == "NORMAL_VOL") regime = MLRegime::NORMAL_VOL;
            else if (regime_str == "HIGH_VOL") regime = MLRegime::HIGH_VOL;
            else if (regime_str == "CRISIS") regime = MLRegime::CRISIS;
            else continue;
            
            setCurve(regime, scale, cap);
        }
        
        std::printf("[KellySizer] Loaded curves from %s\n", path);
        return true;
    }
    
    // =========================================================================
    // Configuration
    // =========================================================================
    
    KellyConfig& config() noexcept { return config_; }
    const KellyConfig& config() const noexcept { return config_; }
    
    void printConfig() const noexcept {
        std::printf("[KellySizer] Configuration:\n");
        std::printf("  max_kelly_raw: %.2f\n", config_.max_kelly_raw);
        std::printf("  max_kelly_final: %.2f\n", config_.max_kelly_final);
        std::printf("  fractional_kelly: %.2f\n", config_.default_fractional_kelly);
        std::printf("  Curves:\n");
        const char* names[] = {"LOW_VOL", "NORMAL_VOL", "HIGH_VOL", "CRISIS"};
        for (int i = 0; i < 4; ++i) {
            std::printf("    %s: scale=%.2f cap=%.2f\n", 
                        names[i], curves_[i].scale, curves_[i].cap);
        }
    }
    
private:
    KellyConfig config_;
    KellyCurve curves_[4];
};

// =============================================================================
// Helper: Quick Kelly calculation without object
// =============================================================================
inline double quickKelly(double prob_win, double expected_R, double avg_loss_R) noexcept {
    if (prob_win <= 0 || prob_win >= 1) return 0.0;
    if (std::fabs(avg_loss_R) < 1e-9) return 0.0;
    
    double b = expected_R / std::fabs(avg_loss_R);
    if (b <= 0) return 0.0;
    
    double kelly = (prob_win * b - (1.0 - prob_win)) / b;
    return std::max(0.0, kelly * 0.5);  // Half Kelly
}

} // namespace ML
} // namespace Chimera
