// =============================================================================
// MLIntegration.hpp - Complete ML Integration for Chimera HFT
// =============================================================================
// PURPOSE: Single header to integrate ML into Chimera's trading flow
// DESIGN:
//   - MLPipeline: Orchestrates all ML components
//   - Slots into existing flow AFTER strategy signal, BEFORE execution
//   - Can be completely disabled (deterministic fallback)
//   - Thread-safe for dual-engine architecture
//
// INTEGRATION POINT:
//   MarketState → Strategy proposes trade → MLPipeline (filter + sizing) → Risk → Execution
//
// COMPONENTS INTEGRATED:
//   - MLFeatureLogger: Binary feature logging
//   - MLInferenceEngine: ONNX model inference
//   - RegimeClassifier: Volatility regime detection
//   - DriftMonitor: Auto-disable on model degradation
//   - ContextualBandit: Aggression optimization
//   - KellySizer: Capital-scaled position sizing
//   - AuditLogger: Full decision chain logging
//
// USAGE:
//   // In engine initialization:
//   MLPipeline ml;
//   ml.configure(MLPipelineConfig{...});
//   ml.start();
//   
//   // In hot path (after strategy signal):
//   MLContext ctx = ml.buildContext(state, signals, tick);
//   MLDecision decision = ml.evaluate(ctx);
//   
//   if (!decision.allow_trade) return;  // ML vetoed
//   
//   double final_size = base_size * decision.size_multiplier;
//   
//   // On trade close:
//   ml.onTradeClose(order_id, realized_R, mfe_R, mae_R, hold_ms);
// =============================================================================
#pragma once

// Include all ML components
#include "MLTypes.hpp"
#include "MLFeatureLogger.hpp"
#include "MLInference.hpp"
#include "RegimeClassifier.hpp"
#include "DriftMonitor.hpp"
#include "ContextualBandit.hpp"
#include "KellySizer.hpp"
#include "AuditLogger.hpp"

#include <memory>
#include <atomic>
#include <chrono>

namespace Chimera {
namespace ML {

// =============================================================================
// ML Pipeline Configuration
// =============================================================================
struct MLPipelineConfig {
    // Model paths
    const char* model_path = "models/active/chimera_ml.onnx";
    const char* feature_log_path = "ml_features.bin";
    const char* audit_log_path = "audit_log.bin";
    const char* kelly_curves_path = "config/kelly_curves.csv";
    
    // Feature toggles
    bool enable_inference = true;       // Run ML inference
    bool enable_feature_logging = true; // Log features for training
    bool enable_audit_logging = true;   // Log full decision chain
    bool enable_drift_monitor = true;   // Auto-disable on drift
    bool enable_bandit = true;          // Contextual bandit sizing
    bool enable_kelly = true;           // Kelly-based sizing
    
    // Fallback behavior when ML disabled/degraded
    bool allow_trades_without_ml = true;  // Trade using deterministic logic
    float fallback_size_mult = 0.5f;      // Reduced size when ML off
    
    // Thresholds
    float min_expected_R = 0.1f;          // Skip if E[R] below this
    float min_prob_positive = 0.45f;      // Skip if P(R>0) below this
    
    // Kelly configuration
    double equity = 10000.0;              // Starting equity
    double avg_loss_R = 1.0;              // Average losing R-multiple
};

// =============================================================================
// ML Context - Input for evaluation
// =============================================================================
struct MLContext {
    // Identification
    uint64_t timestamp_ns;
    uint32_t symbol_id;
    int8_t   proposed_side;  // +1 BUY, -1 SELL
    uint8_t  strategy_id;
    
    // Market state (from MarketStateClassifier)
    MLMarketState state;
    MLTradeIntent intent;
    uint8_t conviction_score;
    
    // Microstructure features
    float atr_multiple;
    float volume_z;
    float range_z;
    float distance_vwap;
    float ofi;
    float vpin;
    float spread_bps;
    float trend_strength;
    
    // Context
    uint16_t minutes_from_open;
    double current_equity;
    double current_drawdown;
    
    // Index for regime (optional)
    double index_drawdown;
    double atr_percentile;
    
    // Build feature vector
    MLFeatureVector toFeatureVector() const noexcept {
        return MLFeatureVector::fromSignals(
            state, intent, MLRegime::NORMAL_VOL,  // Regime set separately
            atr_multiple, volume_z, range_z, distance_vwap,
            ofi, vpin, static_cast<float>(conviction_score),
            spread_bps, trend_strength
        );
    }
};

// =============================================================================
// ML Pipeline - Complete orchestration
// =============================================================================
class MLPipeline {
public:
    MLPipeline() noexcept 
        : running_(false)
        , ml_enabled_(false)
        , total_evaluations_(0)
        , total_vetoes_(0)
        , total_trades_(0)
    {}
    
    ~MLPipeline() {
        stop();
    }
    
    // =========================================================================
    // Lifecycle
    // =========================================================================
    
    bool configure(const MLPipelineConfig& config) noexcept {
        config_ = config;
        
        // Configure Kelly sizer
        if (config_.kelly_curves_path && config_.kelly_curves_path[0]) {
            kelly_.loadCurves(config_.kelly_curves_path);
        }
        
        return true;
    }
    
    bool start() noexcept {
        if (running_.load()) return true;
        
        std::printf("[MLPipeline] Starting...\n");
        
        // Start feature logger
        if (config_.enable_feature_logging) {
            feature_logger_ = std::make_unique<MLFeatureLogger>(config_.feature_log_path);
            if (!feature_logger_->start()) {
                std::fprintf(stderr, "[MLPipeline] Feature logger failed\n");
            }
        }
        
        // Start audit logger
        if (config_.enable_audit_logging) {
            audit_logger_ = std::make_unique<AuditLogger>(config_.audit_log_path);
            if (!audit_logger_->start()) {
                std::fprintf(stderr, "[MLPipeline] Audit logger failed\n");
            }
        }
        
        // Load ML model
        if (config_.enable_inference) {
            if (inference_.loadModel(config_.model_path)) {
                ml_enabled_ = true;
                std::printf("[MLPipeline] ML model loaded\n");
            } else {
                std::fprintf(stderr, "[MLPipeline] ML model load failed (using fallback)\n");
            }
        }
        
        running_.store(true);
        std::printf("[MLPipeline] Started (ML=%s)\n", ml_enabled_ ? "ON" : "OFF");
        return true;
    }
    
    void stop() noexcept {
        if (!running_.load()) return;
        
        running_.store(false);
        
        if (feature_logger_) {
            feature_logger_->stop();
            feature_logger_.reset();
        }
        
        if (audit_logger_) {
            audit_logger_->stop();
            audit_logger_.reset();
        }
        
        std::printf("[MLPipeline] Stopped. Evals=%zu Vetoes=%zu Trades=%zu\n",
                    total_evaluations_.load(), total_vetoes_.load(), total_trades_.load());
    }
    
    // =========================================================================
    // HOT PATH - Evaluate trade proposal
    // =========================================================================
    
    MLDecision evaluate(const MLContext& ctx) noexcept {
        total_evaluations_.fetch_add(1, std::memory_order_relaxed);
        
        MLDecision decision;
        decision.regime_used = current_regime_;
        
        // ── Step 1: Update regime ──
        regime_classifier_.updateATR(ctx.atr_multiple);
        current_regime_ = regime_classifier_.classify(
            ctx.atr_percentile,
            ctx.index_drawdown
        );
        decision.regime_used = current_regime_;
        
        // ── Step 2: Check drift degradation ──
        bool ml_degraded = config_.enable_drift_monitor && drift_monitor_.isDegraded();
        
        // ── Step 3: ML Inference ──
        if (ml_enabled_ && config_.enable_inference && !ml_degraded) {
            MLFeatureVector features = ctx.toFeatureVector();
            decision = inference_.inferWithRegime(features, current_regime_);
        } else {
            // Fallback mode
            decision.ml_active = false;
            decision.allow_trade = config_.allow_trades_without_ml;
            decision.size_multiplier = config_.fallback_size_mult;
            decision.expected_R = 0.0f;
            decision.prob_positive = 0.5f;
        }
        
        // ── Step 4: Threshold checks ──
        if (decision.ml_active) {
            if (decision.expected_R < config_.min_expected_R ||
                decision.prob_positive < config_.min_prob_positive) {
                decision.allow_trade = false;
            }
        }
        
        // ── Step 5: Contextual bandit sizing ──
        if (config_.enable_bandit && decision.allow_trade) {
            double bandit_mult = regime_bandit_.chooseMultiplier(
                current_regime_, ctx.state, decision.expected_R
            );
            decision.size_multiplier *= static_cast<float>(bandit_mult);
        }
        
        // ── Step 6: Kelly sizing ──
        if (config_.enable_kelly && decision.allow_trade) {
            KellyInputs kelly_in;
            kelly_in.prob_win = decision.prob_positive;
            kelly_in.expected_R = decision.expected_R;
            kelly_in.avg_loss_R = config_.avg_loss_R;
            kelly_in.equity = ctx.current_equity;
            kelly_in.drawdown_pct = ctx.current_drawdown;
            kelly_in.regime_mult = 1.0;
            
            double kelly_frac = kelly_.computeFraction(kelly_in, current_regime_);
            // Apply Kelly as additional multiplier (capped)
            double kelly_mult = std::clamp(kelly_frac * 20.0, 0.5, 2.0);
            decision.size_multiplier *= static_cast<float>(kelly_mult);
        }
        
        // ── Step 7: Log features ──
        if (config_.enable_feature_logging && feature_logger_) {
            logFeatures(ctx, decision);
        }
        
        // Track stats
        if (!decision.allow_trade) {
            total_vetoes_.fetch_add(1, std::memory_order_relaxed);
        }
        
        return decision;
    }
    
    // Simpler evaluate for quick checks
    MLDecision evaluate(
        MLMarketState state,
        MLTradeIntent intent,
        uint8_t conviction,
        float ofi,
        float vpin,
        float spread_bps,
        float trend_strength,
        double equity,
        double drawdown
    ) noexcept {
        MLContext ctx;
        ctx.timestamp_ns = nowNs();
        ctx.symbol_id = 0;
        ctx.proposed_side = 1;
        ctx.strategy_id = 0;
        ctx.state = state;
        ctx.intent = intent;
        ctx.conviction_score = conviction;
        ctx.atr_multiple = 1.0f;
        ctx.volume_z = 0.0f;
        ctx.range_z = 0.0f;
        ctx.distance_vwap = 0.0f;
        ctx.ofi = ofi;
        ctx.vpin = vpin;
        ctx.spread_bps = spread_bps;
        ctx.trend_strength = trend_strength;
        ctx.minutes_from_open = 0;
        ctx.current_equity = equity;
        ctx.current_drawdown = drawdown;
        ctx.index_drawdown = 0.0;
        ctx.atr_percentile = 0.5;
        
        return evaluate(ctx);
    }
    
    // =========================================================================
    // Trade Outcome Tracking
    // =========================================================================
    
    // Call when trade closes
    void onTradeClose(
        uint64_t order_id,
        float realized_R,
        float mfe_R,
        float mae_R,
        uint32_t hold_ms
    ) noexcept {
        total_trades_.fetch_add(1, std::memory_order_relaxed);
        
        // Update drift monitor
        if (config_.enable_drift_monitor) {
            // Use last prediction (stored per order) vs actual
            drift_monitor_.observe(last_predicted_R_, realized_R, nowNs());
        }
        
        // Update bandits
        if (config_.enable_bandit) {
            regime_bandit_.update(current_regime_, last_state_, realized_R);
        }
        
        // Log close
        if (config_.enable_audit_logging && audit_logger_) {
            audit_logger_->logClose(order_id, nowNs(), realized_R, mfe_R, mae_R, hold_ms);
        }
    }
    
    // Log order with full audit trail (returns order_id)
    uint64_t logOrder(
        const MLContext& ctx,
        const MLDecision& decision,
        double price,
        double size,
        double stop
    ) noexcept {
        if (!config_.enable_audit_logging || !audit_logger_) {
            return 0;
        }
        
        // Store for drift tracking
        last_predicted_R_ = decision.expected_R;
        last_state_ = ctx.state;
        
        return audit_logger_->logOrder(
            ctx.timestamp_ns,
            ctx.symbol_id,
            ctx.proposed_side,
            price, size, stop,
            ctx.state, ctx.intent, current_regime_,
            ctx.conviction_score, ctx.strategy_id,
            decision,
            0.0f,  // kelly_raw
            0.0f,  // kelly_damped
            1.0f,  // bandit_mult
            static_cast<float>(drift_monitor_.lastRMSE()),
            drift_monitor_.isDegraded()
        );
    }
    
    // =========================================================================
    // Accessors
    // =========================================================================
    
    bool isRunning() const noexcept { return running_.load(); }
    bool isMLEnabled() const noexcept { return ml_enabled_; }
    bool isDegraded() const noexcept { return drift_monitor_.isDegraded(); }
    MLRegime currentRegime() const noexcept { return current_regime_; }
    
    size_t totalEvaluations() const noexcept { return total_evaluations_.load(); }
    size_t totalVetoes() const noexcept { return total_vetoes_.load(); }
    size_t totalTrades() const noexcept { return total_trades_.load(); }
    
    double vetoRate() const noexcept {
        size_t evals = total_evaluations_.load();
        if (evals == 0) return 0.0;
        return static_cast<double>(total_vetoes_.load()) / evals;
    }
    
    // Component access for fine-grained control
    RegimeClassifier& regimeClassifier() noexcept { return regime_classifier_; }
    DriftMonitor& driftMonitor() noexcept { return drift_monitor_; }
    RegimeBandit& bandit() noexcept { return regime_bandit_; }
    KellySizer& kelly() noexcept { return kelly_; }
    MLInferenceEngine& inference() noexcept { return inference_; }
    
    // Enable/disable ML at runtime
    void enableML(bool enable) noexcept { ml_enabled_ = enable; }
    
    // Force drift recovery
    void resetDrift() noexcept { drift_monitor_.reset(); }
    
    // Print stats
    void printStats() const noexcept {
        std::printf("[MLPipeline] Stats:\n");
        std::printf("  Evaluations: %zu\n", total_evaluations_.load());
        std::printf("  Vetoes: %zu (%.1f%%)\n", total_vetoes_.load(), vetoRate() * 100);
        std::printf("  Trades: %zu\n", total_trades_.load());
        std::printf("  Regime: %s\n", regimeStr(current_regime_));
        std::printf("  ML: %s, Degraded: %s\n", 
                    ml_enabled_ ? "ON" : "OFF",
                    drift_monitor_.isDegraded() ? "YES" : "NO");
        if (feature_logger_) {
            std::printf("  Features logged: %zu\n", feature_logger_->recordsWritten());
        }
    }
    
private:
    void logFeatures(const MLContext& ctx, const MLDecision& decision) noexcept {
        if (!feature_logger_) return;
        
        feature_logger_->logEntry(
            ctx.timestamp_ns,
            ctx.symbol_id,
            ctx.state,
            ctx.intent,
            current_regime_,
            ctx.atr_multiple,
            ctx.volume_z,
            ctx.range_z,
            ctx.distance_vwap,
            ctx.ofi,
            ctx.vpin,
            static_cast<float>(ctx.conviction_score),
            ctx.spread_bps,
            ctx.trend_strength,
            ctx.minutes_from_open,
            ctx.proposed_side,
            ctx.strategy_id
        );
    }
    
    static uint64_t nowNs() noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }
    
private:
    MLPipelineConfig config_;
    
    // State
    std::atomic<bool> running_;
    bool ml_enabled_;
    MLRegime current_regime_ = MLRegime::NORMAL_VOL;
    
    // Tracking for drift
    float last_predicted_R_ = 0.0f;
    MLMarketState last_state_ = MLMarketState::DEAD;
    
    // Stats
    std::atomic<size_t> total_evaluations_;
    std::atomic<size_t> total_vetoes_;
    std::atomic<size_t> total_trades_;
    
    // Components
    MLInferenceEngine inference_;
    RegimeClassifier regime_classifier_;
    DriftMonitor drift_monitor_;
    RegimeBandit regime_bandit_;
    KellySizer kelly_;
    
    // Loggers
    std::unique_ptr<MLFeatureLogger> feature_logger_;
    std::unique_ptr<AuditLogger> audit_logger_;
};

} // namespace ML
} // namespace Chimera
