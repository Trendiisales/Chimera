// =============================================================================
// DriftMonitor.hpp - Live Model Drift Detection & Auto-Disable
// =============================================================================
// PURPOSE: Detect when ML predictions no longer match reality and auto-disable
// DESIGN:
//   - Tracks rolling window of (predicted_R, actual_R) pairs
//   - Computes RMSE and correlation over window
//   - Triggers degraded state when metrics exceed thresholds
//   - Automatic recovery when metrics improve
//
// WHY THIS MATTERS:
//   ML models decay. Markets change. This catches:
//   - Regime shifts not in training data
//   - Model overfit becoming visible in production
//   - Data pipeline issues (stale features)
//   - Silent model failures
//
// USAGE:
//   DriftMonitor monitor;
//   
//   // On each trade close:
//   monitor.observe(predicted_R, actual_R);
//   
//   // Before each trade:
//   if (monitor.isDegraded()) {
//       // Fall back to deterministic logic
//       // Don't use ML sizing
//   }
//
// ARCHITECTURE:
//   observe() → rolling window → compute stats → check thresholds
//                                                       ↓
//                                             isDegraded() → disable ML
// =============================================================================
#pragma once

#include <deque>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Chimera {
namespace ML {

// =============================================================================
// Drift Monitor Configuration
// =============================================================================
struct DriftConfig {
    // Window size for rolling statistics
    size_t window_size = 100;
    
    // Minimum observations before checking drift
    size_t min_observations = 50;
    
    // RMSE threshold - above this = degraded
    double rmse_threshold = 1.2;
    
    // Correlation threshold - below this = degraded
    double correlation_threshold = 0.2;
    
    // Mean absolute error threshold
    double mae_threshold = 1.0;
    
    // Number of consecutive degraded checks before triggering
    int degraded_count_threshold = 5;
    
    // Recovery: must stay healthy for this many checks
    int recovery_count_threshold = 10;
    
    // Auto-recovery after this many observations (0 = no auto-recovery)
    size_t auto_recovery_after = 200;
};

// =============================================================================
// Observation Pair
// =============================================================================
struct DriftObservation {
    double predicted;
    double actual;
    uint64_t timestamp_ns;
    
    double error() const { return predicted - actual; }
    double abs_error() const { return std::fabs(error()); }
};

// =============================================================================
// Drift Monitor
// =============================================================================
class DriftMonitor {
public:
    explicit DriftMonitor(const DriftConfig& config = DriftConfig()) noexcept
        : config_(config)
        , is_degraded_(false)
        , degraded_streak_(0)
        , healthy_streak_(0)
        , total_observations_(0)
        , observations_since_degrade_(0)
        , last_rmse_(0.0)
        , last_corr_(1.0)
        , last_mae_(0.0)
    {}
    
    // =========================================================================
    // Core API
    // =========================================================================
    
    // Observe a (predicted, actual) pair on trade close
    void observe(double predicted, double actual, uint64_t ts_ns = 0) noexcept {
        DriftObservation obs{predicted, actual, ts_ns};
        
        window_.push_back(obs);
        if (window_.size() > config_.window_size) {
            window_.pop_front();
        }
        
        total_observations_++;
        if (is_degraded_) {
            observations_since_degrade_++;
        }
        
        // Update stats and check degradation
        updateStats();
        checkDegradation();
        
        // Auto-recovery after enough new observations
        if (is_degraded_ && config_.auto_recovery_after > 0 &&
            observations_since_degrade_ >= config_.auto_recovery_after) {
            std::printf("[DriftMonitor] Auto-recovery after %zu observations\n",
                        observations_since_degrade_);
            forceRecovery();
        }
    }
    
    // Check if ML should be disabled
    bool isDegraded() const noexcept { return is_degraded_; }
    
    // Force recovery (for manual override or testing)
    void forceRecovery() noexcept {
        is_degraded_ = false;
        degraded_streak_ = 0;
        healthy_streak_ = 0;
        observations_since_degrade_ = 0;
    }
    
    // Force degraded state (for testing or external trigger)
    void forceDegrade() noexcept {
        if (!is_degraded_) {
            is_degraded_ = true;
            observations_since_degrade_ = 0;
            std::printf("[DriftMonitor] Manually degraded\n");
        }
    }
    
    // Clear all history
    void reset() noexcept {
        window_.clear();
        is_degraded_ = false;
        degraded_streak_ = 0;
        healthy_streak_ = 0;
        total_observations_ = 0;
        observations_since_degrade_ = 0;
        last_rmse_ = 0.0;
        last_corr_ = 1.0;
        last_mae_ = 0.0;
    }
    
    // =========================================================================
    // Stats Access
    // =========================================================================
    
    size_t windowSize() const noexcept { return window_.size(); }
    size_t totalObservations() const noexcept { return total_observations_; }
    double lastRMSE() const noexcept { return last_rmse_; }
    double lastCorrelation() const noexcept { return last_corr_; }
    double lastMAE() const noexcept { return last_mae_; }
    
    bool hasEnoughData() const noexcept {
        return window_.size() >= config_.min_observations;
    }
    
    // Get configuration (for tuning)
    DriftConfig& config() noexcept { return config_; }
    const DriftConfig& config() const noexcept { return config_; }
    
private:
    void updateStats() noexcept {
        if (window_.size() < 2) return;
        
        // Compute mean
        double sum_pred = 0, sum_actual = 0;
        for (const auto& obs : window_) {
            sum_pred += obs.predicted;
            sum_actual += obs.actual;
        }
        double mean_pred = sum_pred / window_.size();
        double mean_actual = sum_actual / window_.size();
        
        // Compute RMSE, MAE, and correlation components
        double sum_sq_error = 0;
        double sum_abs_error = 0;
        double sum_pred_dev_sq = 0;
        double sum_actual_dev_sq = 0;
        double sum_cross_dev = 0;
        
        for (const auto& obs : window_) {
            double error = obs.error();
            sum_sq_error += error * error;
            sum_abs_error += std::fabs(error);
            
            double pred_dev = obs.predicted - mean_pred;
            double actual_dev = obs.actual - mean_actual;
            sum_pred_dev_sq += pred_dev * pred_dev;
            sum_actual_dev_sq += actual_dev * actual_dev;
            sum_cross_dev += pred_dev * actual_dev;
        }
        
        last_rmse_ = std::sqrt(sum_sq_error / window_.size());
        last_mae_ = sum_abs_error / window_.size();
        
        // Correlation
        double denom = std::sqrt(sum_pred_dev_sq * sum_actual_dev_sq);
        if (denom > 1e-9) {
            last_corr_ = sum_cross_dev / denom;
        } else {
            last_corr_ = 0.0;  // Undefined / no variance
        }
    }
    
    void checkDegradation() noexcept {
        if (!hasEnoughData()) return;
        
        bool currently_bad = 
            (last_rmse_ > config_.rmse_threshold) ||
            (last_corr_ < config_.correlation_threshold) ||
            (last_mae_ > config_.mae_threshold);
        
        if (currently_bad) {
            degraded_streak_++;
            healthy_streak_ = 0;
            
            if (!is_degraded_ && degraded_streak_ >= config_.degraded_count_threshold) {
                is_degraded_ = true;
                observations_since_degrade_ = 0;
                std::printf("[DriftMonitor] DEGRADED: RMSE=%.3f, Corr=%.3f, MAE=%.3f\n",
                            last_rmse_, last_corr_, last_mae_);
            }
        } else {
            healthy_streak_++;
            degraded_streak_ = 0;
            
            if (is_degraded_ && healthy_streak_ >= config_.recovery_count_threshold) {
                is_degraded_ = false;
                std::printf("[DriftMonitor] RECOVERED: RMSE=%.3f, Corr=%.3f, MAE=%.3f\n",
                            last_rmse_, last_corr_, last_mae_);
            }
        }
    }
    
private:
    DriftConfig config_;
    std::deque<DriftObservation> window_;
    bool is_degraded_;
    int degraded_streak_;
    int healthy_streak_;
    size_t total_observations_;
    size_t observations_since_degrade_;
    double last_rmse_;
    double last_corr_;
    double last_mae_;
};

// =============================================================================
// Multi-Regime Drift Monitor - Separate tracking per regime
// =============================================================================
class RegimeDriftMonitor {
public:
    static constexpr size_t NUM_REGIMES = 4;
    
    RegimeDriftMonitor() noexcept {
        for (size_t i = 0; i < NUM_REGIMES; ++i) {
            monitors_[i] = DriftMonitor();
        }
    }
    
    void observe(MLRegime regime, double predicted, double actual, uint64_t ts_ns = 0) noexcept {
        size_t idx = static_cast<size_t>(regime);
        if (idx < NUM_REGIMES) {
            monitors_[idx].observe(predicted, actual, ts_ns);
        }
    }
    
    bool isDegraded(MLRegime regime) const noexcept {
        size_t idx = static_cast<size_t>(regime);
        if (idx < NUM_REGIMES) {
            return monitors_[idx].isDegraded();
        }
        return false;
    }
    
    bool anyDegraded() const noexcept {
        for (const auto& m : monitors_) {
            if (m.isDegraded()) return true;
        }
        return false;
    }
    
    DriftMonitor& get(MLRegime regime) noexcept {
        return monitors_[static_cast<size_t>(regime) % NUM_REGIMES];
    }
    
    const DriftMonitor& get(MLRegime regime) const noexcept {
        return monitors_[static_cast<size_t>(regime) % NUM_REGIMES];
    }
    
    void reset() noexcept {
        for (auto& m : monitors_) {
            m.reset();
        }
    }
    
private:
    std::array<DriftMonitor, NUM_REGIMES> monitors_;
};

} // namespace ML
} // namespace Chimera
