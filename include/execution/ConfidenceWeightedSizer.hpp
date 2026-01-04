// ═══════════════════════════════════════════════════════════════════════════════
// include/execution/ConfidenceWeightedSizer.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE - v4.9.27
// PURPOSE: Confidence-weighted position sizing (replaces binary gating)
// OWNER: Jo
// CREATED: 2026-01-04
//
// v4.9.27 PHASE 4: CONFIDENCE-WEIGHTED SIZING
// ═══════════════════════════════════════════════════════════════════════════════
//
// THE PROBLEM (Binary Gating):
//   Current system uses binary gates:
//     if (confidence < 0.6) return;  // 100% → 0% instantly
//   
//   This loses edge:
//     - 0.59 confidence trade = $0 (rejected)
//     - 0.60 confidence trade = full size (accepted)
//   
//   The 0.59 trade might have positive expectancy at reduced size!
//
// THE SOLUTION (Smooth Scaling):
//   Replace binary gate with continuous scaling:
//     size = base_size × f(confidence) × regime_mult × exec_health
//   
//   Where f(confidence) is quadratic for risk-aversion:
//     f(c) = c² (penalizes uncertainty more than linearly)
//
// FACTORS COMBINED:
//   1. CONFIDENCE (c²)
//      - 1.0 → 100% size
//      - 0.8 → 64% size
//      - 0.5 → 25% size
//      - 0.3 → 9% size (natural floor, no hard cutoff)
//
//   2. REGIME MULTIPLIER
//      - From MarketRegime: TRENDING=1.2, RANGE=1.0, VOLATILE=0.5
//
//   3. EXECUTION HEALTH
//      - From ExecutionGovernor: HEALTHY=1.0, DEGRADED=0.5, HALTED=0.0
//
//   4. ALPHA TRUST
//      - From AlphaRetirement: Recent performance → multiplier
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <atomic>

namespace Chimera {

// ─────────────────────────────────────────────────────────────────────────────
// Sizing Factors (for transparency/logging)
// ─────────────────────────────────────────────────────────────────────────────
struct SizingFactors {
    double base_size;
    double confidence;
    double confidence_factor;       // c² after quadratic scaling
    double regime_mult;
    double exec_health;
    double alpha_trust;
    double daily_loss_throttle;
    double final_size;
    
    SizingFactors() 
        : base_size(0.0), confidence(1.0), confidence_factor(1.0)
        , regime_mult(1.0), exec_health(1.0), alpha_trust(1.0)
        , daily_loss_throttle(1.0), final_size(0.0) {}
    
    void print() const {
        printf("[SIZER] base=%.5f conf=%.2f(×%.2f) regime=%.2f exec=%.2f trust=%.2f dd=%.2f → final=%.5f\n",
               base_size, confidence, confidence_factor, 
               regime_mult, exec_health, alpha_trust, daily_loss_throttle,
               final_size);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Confidence Weighted Sizer (Singleton)
// ─────────────────────────────────────────────────────────────────────────────
class ConfidenceWeightedSizer {
public:
    static ConfidenceWeightedSizer& instance() {
        static ConfidenceWeightedSizer s;
        return s;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // CORE COMPUTATION
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] double compute(
        double base_size,
        double confidence,          // 0.0 - 1.0
        double regime_multiplier,   // From MarketRegime
        double execution_health,    // From ExecutionGovernor (0=HALTED, 0.5=DEGRADED, 1=HEALTHY)
        double alpha_trust = 1.0,   // From AlphaRetirement (recent perf)
        double daily_loss_throttle = 1.0  // From drawdown
    ) const noexcept {
        // ───────────────────────────────────────────────────────────────────
        // STEP 1: Confidence → Quadratic scaling
        // ───────────────────────────────────────────────────────────────────
        double conf_clamped = std::clamp(confidence, 0.0, 1.0);
        double conf_factor = conf_clamped * conf_clamped;  // Quadratic
        
        // ───────────────────────────────────────────────────────────────────
        // STEP 2: Apply minimum threshold (avoid dust trades)
        // ───────────────────────────────────────────────────────────────────
        if (conf_factor < min_confidence_factor_) {
            return 0.0;  // Below noise floor
        }
        
        // ───────────────────────────────────────────────────────────────────
        // STEP 3: Multiply all factors
        // ───────────────────────────────────────────────────────────────────
        double final_size = base_size 
                          * conf_factor 
                          * std::clamp(regime_multiplier, 0.0, max_regime_mult_)
                          * std::clamp(execution_health, 0.0, 1.0)
                          * std::clamp(alpha_trust, 0.0, max_alpha_trust_)
                          * std::clamp(daily_loss_throttle, 0.0, 1.0);
        
        // ───────────────────────────────────────────────────────────────────
        // STEP 4: Cap at max size
        // ───────────────────────────────────────────────────────────────────
        final_size = std::min(final_size, max_position_size_);
        
        // ───────────────────────────────────────────────────────────────────
        // STEP 5: Round to precision
        // ───────────────────────────────────────────────────────────────────
        final_size = roundToPrecision(final_size, size_precision_);
        
        // Increment counter
        total_sized_++;
        
        return final_size;
    }
    
    // Full compute with factors output (for logging/debugging)
    [[nodiscard]] double computeWithFactors(
        double base_size,
        double confidence,
        double regime_multiplier,
        double execution_health,
        double alpha_trust,
        double daily_loss_throttle,
        SizingFactors& factors_out
    ) const noexcept {
        factors_out.base_size = base_size;
        factors_out.confidence = confidence;
        factors_out.confidence_factor = std::clamp(confidence, 0.0, 1.0) * std::clamp(confidence, 0.0, 1.0);
        factors_out.regime_mult = regime_multiplier;
        factors_out.exec_health = execution_health;
        factors_out.alpha_trust = alpha_trust;
        factors_out.daily_loss_throttle = daily_loss_throttle;
        
        factors_out.final_size = compute(
            base_size, confidence, regime_multiplier, 
            execution_health, alpha_trust, daily_loss_throttle
        );
        
        return factors_out.final_size;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // SIMPLIFIED INTERFACE (for common cases)
    // ═══════════════════════════════════════════════════════════════════════
    
    // Just confidence scaling (other factors = 1.0)
    [[nodiscard]] double scaleByConfidence(double base_size, double confidence) const noexcept {
        return compute(base_size, confidence, 1.0, 1.0, 1.0, 1.0);
    }
    
    // Confidence + regime (execution/trust = 1.0)
    [[nodiscard]] double scaleByConfidenceAndRegime(
        double base_size, double confidence, double regime_mult
    ) const noexcept {
        return compute(base_size, confidence, regime_mult, 1.0, 1.0, 1.0);
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // REGIME MULTIPLIERS (from MarketRegime)
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] static double regimeMultiplier(int regime_code) noexcept {
        switch (regime_code) {
            case 0: return 1.0;   // RANGE_BOUND
            case 1: return 1.2;   // TRENDING_UP
            case 2: return 1.2;   // TRENDING_DOWN
            case 3: return 0.5;   // HIGH_VOLATILITY
            case 4: return 0.3;   // CRISIS
            default: return 1.0;
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // EXECUTION HEALTH (from ExecutionGovernor)
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] static double executionHealth(int venue_state) noexcept {
        switch (venue_state) {
            case 0: return 1.0;   // HEALTHY
            case 1: return 0.5;   // DEGRADED
            case 2: return 0.0;   // HALTED
            case 3: return 0.3;   // RECOVERY_COOLDOWN
            default: return 0.5;
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // CONFIGURATION
    // ═══════════════════════════════════════════════════════════════════════
    
    void configure(
        double min_conf_factor,     // Below this → 0 (default 0.09 = conf 0.3)
        double max_regime_mult,     // Cap regime multiplier (default 1.5)
        double max_alpha_trust,     // Cap alpha trust boost (default 1.5)
        double max_pos_size,        // Absolute max position (default 0.01)
        int precision               // Decimal places for rounding (default 5)
    ) {
        min_confidence_factor_ = min_conf_factor;
        max_regime_mult_ = max_regime_mult;
        max_alpha_trust_ = max_alpha_trust;
        max_position_size_ = max_pos_size;
        size_precision_ = precision;
        
        printf("[SIZER] Configured: min_conf=%.2f max_regime=%.1f max_trust=%.1f max_pos=%.5f prec=%d\n",
               min_conf_factor, max_regime_mult, max_alpha_trust, max_pos_size, precision);
    }
    
    void setMaxPositionSize(double max_size) {
        max_position_size_ = max_size;
    }
    
    void setMinConfidenceFactor(double min_factor) {
        min_confidence_factor_ = min_factor;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // STATISTICS
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] uint64_t totalSized() const noexcept {
        return total_sized_;
    }
    
    void printConfig() const {
        printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
        printf("║ CONFIDENCE-WEIGHTED SIZER CONFIG (v4.9.27)                           ║\n");
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        printf("║ Min Confidence Factor: %.2f (conf %.2f²)                             \n",
               min_confidence_factor_, std::sqrt(min_confidence_factor_));
        printf("║ Max Regime Multiplier: %.1f                                          \n", max_regime_mult_);
        printf("║ Max Alpha Trust:       %.1f                                          \n", max_alpha_trust_);
        printf("║ Max Position Size:     %.5f                                        \n", max_position_size_);
        printf("║ Size Precision:        %d decimals                                   \n", size_precision_);
        printf("║ Total Sized:           %llu                                          \n",
               static_cast<unsigned long long>(total_sized_));
        printf("╚══════════════════════════════════════════════════════════════════════╝\n");
    }
    
    // Example confidence scaling table
    void printScalingTable() const {
        printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
        printf("║ CONFIDENCE SCALING TABLE (Quadratic: factor = conf²)                 ║\n");
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        printf("║ Confidence │ Factor │ Size (base=0.001)                              ║\n");
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        
        double base = 0.001;
        for (double c = 1.0; c >= 0.2; c -= 0.1) {
            double factor = c * c;
            double size = scaleByConfidence(base, c);
            const char* note = "";
            if (factor < min_confidence_factor_) note = " (below threshold)";
            printf("║    %.1f     │  %.2f  │  %.6f%s\n", c, factor, size, note);
        }
        printf("╚══════════════════════════════════════════════════════════════════════╝\n");
    }

private:
    ConfidenceWeightedSizer() 
        : min_confidence_factor_(0.09)
        , max_regime_mult_(1.5)
        , max_alpha_trust_(1.5)
        , max_position_size_(0.01)
        , size_precision_(5)
        , total_sized_(0) {}
    
    static double roundToPrecision(double value, int precision) noexcept {
        double mult = std::pow(10.0, precision);
        return std::round(value * mult) / mult;
    }
    
    // Configuration
    double min_confidence_factor_;   // conf < 0.3 → 0
    double max_regime_mult_;         // Cap regime boost
    double max_alpha_trust_;         // Cap trust boost
    double max_position_size_;       // Absolute cap
    int size_precision_;             // Round to 5 decimals
    
    // Stats
    mutable uint64_t total_sized_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Convenience alias
// ─────────────────────────────────────────────────────────────────────────────
inline ConfidenceWeightedSizer& Sizer() {
    return ConfidenceWeightedSizer::instance();
}

} // namespace Chimera
