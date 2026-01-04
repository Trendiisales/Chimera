// =============================================================================
// MLDriftGuard.hpp - ML Drift Detection & Kill Switch (v4.6.0)
// =============================================================================
// PURPOSE: Watch ML output distribution, not just PnL
//          Prevents "slow bleed death" weeks
//
// KILL CONDITIONS:
//   - Rolling q50 collapses (expectancy gone)
//   - Rolling q10 drops (tail risk widening)
//   - Distribution widens abnormally (model confused)
//
// WARMUP: Requires 500 samples before ANY kill/throttle decision
//         This prevents false kills after restart or at session start
//
// This is NOT optional. It saves accounts.
// =============================================================================
#pragma once

#include "MLModel.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>

namespace Chimera {
namespace ML {

// =============================================================================
// Exponential Moving Average Helper
// =============================================================================
class EMA {
public:
    explicit EMA(double alpha = 0.01) noexcept 
        : alpha_(alpha), value_(0.0), initialized_(false) {}
    
    void update(double x) noexcept {
        if (!initialized_) {
            value_ = x;
            initialized_ = true;
        } else {
            value_ = alpha_ * x + (1.0 - alpha_) * value_;
        }
    }
    
    double value() const noexcept { return value_; }
    bool initialized() const noexcept { return initialized_; }
    void reset() noexcept { initialized_ = false; value_ = 0.0; }
    
private:
    double alpha_;
    double value_;
    bool initialized_;
};

// =============================================================================
// ML Drift Guard - Watches ML Health, Triggers Kill/Throttle
// =============================================================================
class MLDriftGuard {
public:
    // Thresholds (can be tuned)
    struct Config {
        double panic_q10 = -2.0;        // Kill if rolling q10 drops below this
        double min_expectancy = 0.2;    // Throttle if rolling q50 drops below this
        double max_iqr_expansion = 3.0; // Kill if IQR widens beyond baseline * this
        double ema_alpha = 0.01;        // Smoothing factor (0.01 = slow, 0.1 = fast)
        uint64_t min_samples = 500;     // WARMUP: Minimum samples before acting
        uint64_t baseline_samples = 100;// Samples needed to establish baseline
    };
    
    MLDriftGuard() noexcept 
        : config_{}
        , rolling_q10_(config_.ema_alpha)
        , rolling_q50_(config_.ema_alpha)
        , rolling_iqr_(config_.ema_alpha)
        , baseline_iqr_(0.0)
        , kill_(false)
        , throttle_(false)
        , samples_(0)
    {}
    
    explicit MLDriftGuard(const Config& cfg) noexcept 
        : config_(cfg)
        , rolling_q10_(cfg.ema_alpha)
        , rolling_q50_(cfg.ema_alpha)
        , rolling_iqr_(cfg.ema_alpha)
        , baseline_iqr_(0.0)
        , kill_(false)
        , throttle_(false)
        , samples_(0)
    {}
    
    // =========================================================================
    // Update with new ML output (call after each MLGate evaluation)
    // =========================================================================
    void update(const MLQuantiles& q) noexcept {
        samples_.fetch_add(1, std::memory_order_relaxed);
        uint64_t current_samples = samples_.load(std::memory_order_relaxed);
        
        rolling_q10_.update(q.q10);
        rolling_q50_.update(q.q50);
        rolling_iqr_.update(q.iqr());
        
        // Set baseline IQR from first N samples
        if (current_samples == config_.baseline_samples) {
            baseline_iqr_ = rolling_iqr_.value();
            std::printf("[MLDriftGuard] Baseline IQR set: %.4f after %lu samples\n", 
                baseline_iqr_, (unsigned long)current_samples);
        }
        
        // =====================================================================
        // WARMUP CHECK: Do NOT kill/throttle until we have enough samples
        // This prevents false kills after restart or at session start
        // =====================================================================
        if (current_samples < config_.min_samples) {
            return;  // No kill/throttle decisions during warmup
        }
        
        checkConditions();
    }
    
    // =========================================================================
    // State Queries
    // =========================================================================
    bool kill() const noexcept { return kill_.load(std::memory_order_acquire); }
    bool throttle() const noexcept { return throttle_.load(std::memory_order_acquire); }
    bool inWarmup() const noexcept { 
        return samples_.load(std::memory_order_relaxed) < config_.min_samples; 
    }
    
    const char* killReason() const noexcept { return kill_reason_; }
    const char* throttleReason() const noexcept { return throttle_reason_; }
    
    // =========================================================================
    // Rolling Values (for dashboard)
    // =========================================================================
    double rollingQ10() const noexcept { return rolling_q10_.value(); }
    double rollingQ50() const noexcept { return rolling_q50_.value(); }
    double rollingIQR() const noexcept { return rolling_iqr_.value(); }
    double baselineIQR() const noexcept { return baseline_iqr_; }
    uint64_t samples() const noexcept { return samples_.load(); }
    
    // =========================================================================
    // Reset (use ONLY on engine restart)
    // =========================================================================
    // RESET BEHAVIOR:
    //   - Reset on engine restart: YES (required to clear stale state)
    //   - Reset on symbol re-enable: NO (drift continuity needed)
    //   - Reset on daily session rollover: NO (drift tracks across sessions)
    //
    // We want drift continuity across sessions to catch slow-moving problems.
    // Only a full engine restart should clear drift state.
    // =========================================================================
    void reset() noexcept {
        rolling_q10_.reset();
        rolling_q50_.reset();
        rolling_iqr_.reset();
        baseline_iqr_ = 0.0;
        kill_.store(false);
        throttle_.store(false);
        samples_.store(0);
        kill_reason_ = nullptr;
        throttle_reason_ = nullptr;
        std::printf("[MLDriftGuard] Reset - entering warmup (need %lu samples)\n", 
            (unsigned long)config_.min_samples);
    }
    
    // Manual override
    void clearKill() noexcept { 
        kill_.store(false); 
        kill_reason_ = nullptr;
    }
    void clearThrottle() noexcept { 
        throttle_.store(false); 
        throttle_reason_ = nullptr;
    }
    
    // =========================================================================
    // Stats
    // =========================================================================
    void printStatus() const {
        uint64_t s = samples_.load();
        bool warmup = s < config_.min_samples;
        
        std::printf("[MLDriftGuard] samples=%lu%s q10=%.3f q50=%.3f iqr=%.3f (baseline=%.3f) kill=%s throttle=%s\n",
            (unsigned long)s,
            warmup ? " [WARMUP]" : "",
            rolling_q10_.value(),
            rolling_q50_.value(),
            rolling_iqr_.value(),
            baseline_iqr_,
            kill() ? "YES" : "no",
            throttle() ? "YES" : "no");
    }
    
private:
    void checkConditions() noexcept {
        // =====================================================================
        // KILL: Rolling q10 collapsed (tail risk explosion)
        // =====================================================================
        if (rolling_q10_.value() < config_.panic_q10) {
            if (!kill_.load()) {
                kill_.store(true, std::memory_order_release);
                kill_reason_ = "Q10_COLLAPSED";
                std::printf("[MLDriftGuard] ⚠️ KILL TRIGGERED: q10=%.3f < %.3f\n",
                    rolling_q10_.value(), config_.panic_q10);
            }
        }
        
        // =====================================================================
        // KILL: IQR expanded beyond tolerance (model confused)
        // =====================================================================
        if (baseline_iqr_ > 0.001) {
            double iqr_ratio = rolling_iqr_.value() / baseline_iqr_;
            if (iqr_ratio > config_.max_iqr_expansion) {
                if (!kill_.load()) {
                    kill_.store(true, std::memory_order_release);
                    kill_reason_ = "IQR_EXPLOSION";
                    std::printf("[MLDriftGuard] ⚠️ KILL TRIGGERED: iqr_ratio=%.2f > %.2f\n",
                        iqr_ratio, config_.max_iqr_expansion);
                }
            }
        }
        
        // =====================================================================
        // THROTTLE: Expectancy dropped (edge eroding)
        // =====================================================================
        if (rolling_q50_.value() < config_.min_expectancy) {
            if (!throttle_.load()) {
                throttle_.store(true, std::memory_order_release);
                throttle_reason_ = "EXPECTANCY_LOW";
                std::printf("[MLDriftGuard] ⚠️ THROTTLE: q50=%.3f < %.3f\n",
                    rolling_q50_.value(), config_.min_expectancy);
            }
        } else {
            // Clear throttle if expectancy recovers
            if (throttle_.load() && rolling_q50_.value() > config_.min_expectancy * 1.2) {
                throttle_.store(false, std::memory_order_release);
                throttle_reason_ = nullptr;
                std::printf("[MLDriftGuard] Throttle cleared: q50 recovered to %.3f\n",
                    rolling_q50_.value());
            }
        }
    }
    
private:
    Config config_;
    
    EMA rolling_q10_;
    EMA rolling_q50_;
    EMA rolling_iqr_;
    double baseline_iqr_;
    
    std::atomic<bool> kill_;
    std::atomic<bool> throttle_;
    std::atomic<uint64_t> samples_;
    
    const char* kill_reason_ = nullptr;
    const char* throttle_reason_ = nullptr;
};

// =============================================================================
// Global Drift Guard Instance
// =============================================================================
inline MLDriftGuard& getMLDriftGuard() {
    static MLDriftGuard instance;
    return instance;
}

} // namespace ML
} // namespace Chimera
