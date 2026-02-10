// =============================================================================
// MLInference.hpp - ONNX Runtime Inference Engine
// =============================================================================
// PURPOSE: Load trained ONNX models and run inference in <10μs
// DESIGN:
//   - Single model or regime-routed multi-model
//   - v4.8.0: FAIL-CLOSED in LIVE mode (returns allow_trade=false on any error)
//   - No Python in production hot path
//   - Thread-safe for multi-engine use
//
// FALLBACK:
//   v4.8.0: If ONNX_RUNTIME_AVAILABLE is not defined:
//     - LIVE mode: FAIL-CLOSED (allow_trade=false, size_multiplier=0)
//     - BACKTEST/SIM mode: Allow trade with neutral multiplier
//
// CHANGELOG:
//   v4.8.0: CRITICAL SAFETY FIX - Fail-closed in LIVE mode
// =============================================================================
#pragma once

#include "MLTypes.hpp"
#include <vector>
#include <array>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>

// Check if ONNX Runtime is available
#ifdef ONNX_RUNTIME_AVAILABLE
#include <onnxruntime_cxx_api.h>
#endif

namespace Chimera {
namespace ML {

// =============================================================================
// EXECUTION MODE - v4.8.0 (controls fail-open vs fail-closed behavior)
// =============================================================================
enum class ExecutionMode : uint8_t {
    LIVE = 0,       // Production - FAIL-CLOSED (no trades on ML error)
    BACKTEST = 1,   // Simulation - fail-open allowed
    PAPER = 2       // Paper trading - FAIL-CLOSED (treat as LIVE)
};

// =============================================================================
// Feature Vector Builder - Prepares input for inference
// =============================================================================
struct MLFeatureVector {
    static constexpr size_t NUM_FEATURES = 12;
    
    std::array<float, NUM_FEATURES> data;
    
    MLFeatureVector() noexcept : data{} {}
    
    // Build from MLFeatureRecord (matches actual struct in MLTypes.hpp)
    static MLFeatureVector fromRecord(const MLFeatureRecord& rec) noexcept {
        MLFeatureVector fv;
        fv.data[0]  = static_cast<float>(rec.state);
        fv.data[1]  = static_cast<float>(rec.intent);
        fv.data[2]  = static_cast<float>(rec.regime);
        fv.data[3]  = static_cast<float>(rec.side);
        fv.data[4]  = rec.ofi;
        fv.data[5]  = rec.vpin;
        fv.data[6]  = rec.spread_bps;
        fv.data[7]  = rec.conviction_score;
        fv.data[8]  = static_cast<float>(rec.minutes_from_open);
        fv.data[9]  = static_cast<float>(rec.strategy_id);
        fv.data[10] = 0.0f;  // Reserved
        fv.data[11] = 0.0f;  // Reserved
        return fv;
    }
    
    // Build from raw microstructure signals
    static MLFeatureVector fromSignals(
        MLMarketState state, MLTradeIntent intent, MLRegime regime, int8_t side,
        float ofi, float vpin, float spread_bps, float conviction,
        uint16_t minutes_from_open, uint8_t strategy_id
    ) noexcept {
        MLFeatureVector fv;
        fv.data[0]  = static_cast<float>(state);
        fv.data[1]  = static_cast<float>(intent);
        fv.data[2]  = static_cast<float>(regime);
        fv.data[3]  = static_cast<float>(side);
        fv.data[4]  = ofi;
        fv.data[5]  = vpin;
        fv.data[6]  = spread_bps;
        fv.data[7]  = conviction;
        fv.data[8]  = static_cast<float>(minutes_from_open);
        fv.data[9]  = static_cast<float>(strategy_id);
        fv.data[10] = 0.0f;
        fv.data[11] = 0.0f;
        return fv;
    }
    
    const float* ptr() const noexcept { return data.data(); }
    size_t size() const noexcept { return NUM_FEATURES; }
};

// =============================================================================
// ML Inference Engine
// =============================================================================
class MLInferenceEngine {
public:
    static constexpr size_t NUM_REGIMES = 4;
    static constexpr float DEFAULT_EXPECTED_R = 0.0f;
    static constexpr float MIN_ALLOW_THRESHOLD = 0.1f;
    static constexpr float MAX_SIZE_MULT = 2.5f;
    static constexpr float MIN_SIZE_MULT = 0.25f;
    
    MLInferenceEngine() noexcept 
        : initialized_(false)
        , execution_mode_(ExecutionMode::LIVE)  // v4.8.0: Default to LIVE (safe)
        , inference_count_(0)
        , inference_time_total_ns_(0)
        , fail_closed_count_(0)
    {
#ifdef ONNX_RUNTIME_AVAILABLE
        try {
            env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "chimera_ml");
            Ort::SessionOptions opts;
            opts.SetIntraOpNumThreads(1);
            opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);
            session_opts_ = std::move(opts);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[MLInferenceEngine] ONNX init failed: %s\n", e.what());
        }
#endif
    }
    
    ~MLInferenceEngine() = default;
    
    // =========================================================================
    // v4.8.0: Execution Mode Configuration
    // =========================================================================
    void setExecutionMode(ExecutionMode mode) noexcept { 
        execution_mode_ = mode;
        std::printf("[MLInferenceEngine] Execution mode set to: %s\n",
                   mode == ExecutionMode::LIVE ? "LIVE (FAIL-CLOSED)" :
                   mode == ExecutionMode::BACKTEST ? "BACKTEST (fail-open)" :
                   "PAPER (FAIL-CLOSED)");
    }
    
    ExecutionMode getExecutionMode() const noexcept { return execution_mode_; }
    
    bool isLiveMode() const noexcept {
        return execution_mode_ == ExecutionMode::LIVE || 
               execution_mode_ == ExecutionMode::PAPER;
    }
    
    // =========================================================================
    // Model Loading
    // =========================================================================
    
    // Load single model (all regimes use same model)
    bool loadModel(const char* path) noexcept {
#ifdef ONNX_RUNTIME_AVAILABLE
        std::lock_guard<std::mutex> lock(mutex_);
        try {
            sessions_[0] = std::make_unique<Ort::Session>(*env_, path, session_opts_);
            initialized_ = true;
            std::printf("[MLInferenceEngine] Loaded model: %s\n", path);
            return true;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[MLInferenceEngine] Load failed: %s - %s\n", path, e.what());
            return false;
        }
#else
        (void)path;
        // v4.8.0: CRITICAL - Mark as initialized but inference will use mode-aware defaults
        std::printf("[MLInferenceEngine] ONNX Runtime not available - ML will use mode-aware defaults\n");
        if (isLiveMode()) {
            std::printf("[MLInferenceEngine] *** LIVE MODE: FAIL-CLOSED - trades blocked without ML ***\n");
        } else {
            std::printf("[MLInferenceEngine] BACKTEST MODE: fail-open allowed\n");
        }
        initialized_ = true;
        return true;
#endif
    }
    
    // Load regime-specific model
    bool loadRegimeModel(MLRegime regime, const char* path) noexcept {
#ifdef ONNX_RUNTIME_AVAILABLE
        std::lock_guard<std::mutex> lock(mutex_);
        size_t idx = static_cast<size_t>(regime);
        if (idx >= NUM_REGIMES) return false;
        
        try {
            sessions_[idx] = std::make_unique<Ort::Session>(*env_, path, session_opts_);
            std::printf("[MLInferenceEngine] Loaded regime model [%s]: %s\n", 
                        regimeStr(regime), path);
            return true;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[MLInferenceEngine] Load failed [%s]: %s - %s\n", 
                         regimeStr(regime), path, e.what());
            return false;
        }
#else
        (void)regime;
        (void)path;
        return true;
#endif
    }
    
    // =========================================================================
    // Inference (Hot Path)
    // =========================================================================
    
    // Infer with single model
    MLDecision infer(const MLFeatureVector& features) noexcept {
        return inferWithRegime(features, MLRegime::NORMAL_VOL);
    }
    
    // Infer with regime-routed model
    MLDecision inferWithRegime(const MLFeatureVector& features, MLRegime regime) noexcept {
        MLDecision decision;
        decision.regime_used = regime;
        
        // =====================================================================
        // v4.8.0: FAIL-CLOSED BEHAVIOR
        // If not initialized in LIVE mode, block all trades
        // =====================================================================
        if (!initialized_) {
            if (isLiveMode()) {
                // FAIL-CLOSED: No trades without working ML
                decision.ml_active = false;
                decision.allow_trade = false;
                decision.size_multiplier = 0.0f;
                fail_closed_count_.fetch_add(1, std::memory_order_relaxed);
                return decision;
            } else {
                // Backtest: Allow trade with neutral settings
                decision.ml_active = false;
                decision.allow_trade = true;
                decision.size_multiplier = 1.0f;
                return decision;
            }
        }
        
#ifdef ONNX_RUNTIME_AVAILABLE
        auto start = std::chrono::steady_clock::now();
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        size_t idx = static_cast<size_t>(regime);
        if (idx >= NUM_REGIMES) idx = 1;  // Fall back to NORMAL_VOL
        
        // Use regime model if available, otherwise use base model
        Ort::Session* session = sessions_[idx].get();
        if (!session) session = sessions_[0].get();
        if (!session) {
            // v4.8.0: No session available - FAIL-CLOSED in LIVE
            if (isLiveMode()) {
                decision.ml_active = false;
                decision.allow_trade = false;
                decision.size_multiplier = 0.0f;
                fail_closed_count_.fetch_add(1, std::memory_order_relaxed);
            } else {
                decision.ml_active = false;
                decision.allow_trade = true;
                decision.size_multiplier = 1.0f;
            }
            return decision;
        }
        
        try {
            // Prepare input tensor
            std::array<int64_t, 2> input_shape = {1, (int64_t)features.size()};
            Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(
                OrtArenaAllocator, OrtMemTypeDefault);
            
            Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
                mem_info,
                const_cast<float*>(features.ptr()),
                features.size(),
                input_shape.data(),
                2
            );
            
            // Run inference
            const char* input_names[] = {"float_input"};
            const char* output_names[] = {"variable"};
            
            auto outputs = session->Run(
                Ort::RunOptions{nullptr},
                input_names, &input_tensor, 1,
                output_names, 1
            );
            
            // Parse output
            float* output_data = outputs[0].GetTensorMutableData<float>();
            decision.expected_R = output_data[0];
            
            // Derive other decision fields
            decision.ml_active = true;
            decision.allow_trade = (decision.expected_R > MIN_ALLOW_THRESHOLD);
            
            // Size multiplier: scale based on expected R
            if (decision.expected_R <= 0.0f) {
                decision.size_multiplier = MIN_SIZE_MULT;
            } else {
                decision.size_multiplier = std::clamp(
                    0.5f + decision.expected_R * 0.5f,
                    MIN_SIZE_MULT, MAX_SIZE_MULT
                );
            }
            
            // Prob positive approximation (sigmoid of expected_R)
            decision.prob_positive = 1.0f / (1.0f + std::exp(-decision.expected_R * 2.0f));
            
            // Quantiles approximated from expected_R
            decision.q25 = decision.expected_R * 0.5f;
            decision.q50 = decision.expected_R;
            decision.q75 = decision.expected_R * 1.5f;
            
            decision.model_confidence = std::clamp(
                std::fabs(decision.expected_R) / 2.0f, 0.0f, 1.0f
            );
            
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[MLInferenceEngine] Inference error: %s\n", e.what());
            
            // ═══════════════════════════════════════════════════════════════
            // v4.8.0: CRITICAL SAFETY FIX
            // On inference exception in LIVE mode -> FAIL-CLOSED
            // ═══════════════════════════════════════════════════════════════
            if (isLiveMode()) {
                decision.ml_active = false;
                decision.allow_trade = false;
                decision.size_multiplier = 0.0f;
                fail_closed_count_.fetch_add(1, std::memory_order_relaxed);
                std::fprintf(stderr, "[MLInferenceEngine] *** FAIL-CLOSED: Trade blocked due to ML error ***\n");
            } else {
                // Backtest: Allow with neutral settings
                decision.ml_active = false;
                decision.allow_trade = true;
                decision.size_multiplier = 1.0f;
            }
        }
        
        auto end = std::chrono::steady_clock::now();
        uint64_t elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - start).count();
        
        inference_count_++;
        inference_time_total_ns_ += elapsed_ns;
        
#else
        // ═══════════════════════════════════════════════════════════════
        // ONNX NOT AVAILABLE - EXPLICIT FAIL-CLOSED MODE
        // SPEC: LIVE = FATAL rejection, BACKTEST = neutral pass-through
        // ═══════════════════════════════════════════════════════════════
        static bool startup_warned = false;
        if (!startup_warned) {
            fprintf(stderr, "╔═══════════════════════════════════════════════════════════════╗\n");
            fprintf(stderr, "║  ⛔ ML INFERENCE: ONNX RUNTIME NOT AVAILABLE                  ║\n");
            fprintf(stderr, "║  ⛔ LIVE MODE WILL REJECT ALL TRADES                          ║\n");
            fprintf(stderr, "║  ⛔ Build with -DONNX_RUNTIME_AVAILABLE for ML                ║\n");
            fprintf(stderr, "╚═══════════════════════════════════════════════════════════════╝\n");
            startup_warned = true;
        }
        
        if (isLiveMode()) {
            // LIVE without ONNX: FAIL-CLOSED (SPEC-MANDATED)
            // LOUD: Print every rejection so operator knows ML is blocking
            uint64_t count = fail_closed_count_.fetch_add(1, std::memory_order_relaxed);
            if (count < 10 || count % 100 == 0) {
                fprintf(stderr, "[ML-FATAL] TRADE BLOCKED: No ONNX runtime (count=%lu)\n", 
                        (unsigned long)(count + 1));
            }
            decision.ml_active = false;
            decision.allow_trade = false;
            decision.size_multiplier = 0.0f;
            decision.expected_R = 0.0f;
            decision.prob_positive = 0.5f;
            decision.model_confidence = 0.0f;
        } else {
            // Backtest without ONNX: Allow with neutral (explicit, not default)
            decision.ml_active = false;
            decision.allow_trade = true;
            decision.size_multiplier = 1.0f;
            decision.expected_R = 0.0f;
            decision.prob_positive = 0.5f;
            decision.model_confidence = 0.5f;
        }
        inference_count_++;
#endif
        
        return decision;
    }
    
    // =========================================================================
    // Stats
    // =========================================================================
    
    bool isInitialized() const noexcept { return initialized_; }
    uint64_t inferenceCount() const noexcept { return inference_count_; }
    uint64_t failClosedCount() const noexcept { return fail_closed_count_.load(std::memory_order_relaxed); }
    
    double avgInferenceUs() const noexcept {
        if (inference_count_ == 0) return 0.0;
        return static_cast<double>(inference_time_total_ns_) / inference_count_ / 1000.0;
    }
    
    void printStatus() const {
        std::printf("[MLInferenceEngine] Mode=%s Init=%s Inferences=%lu FailClosed=%lu AvgLatency=%.2fus\n",
                   isLiveMode() ? "LIVE" : "BACKTEST",
                   initialized_ ? "YES" : "NO",
                   static_cast<unsigned long>(inference_count_),
                   static_cast<unsigned long>(fail_closed_count_.load()),
                   avgInferenceUs());
    }
    
private:
#ifdef ONNX_RUNTIME_AVAILABLE
    std::unique_ptr<Ort::Env> env_;
    Ort::SessionOptions session_opts_;
    std::array<std::unique_ptr<Ort::Session>, NUM_REGIMES> sessions_;
#endif
    
    std::mutex mutex_;
    bool initialized_;
    ExecutionMode execution_mode_;
    uint64_t inference_count_;
    uint64_t inference_time_total_ns_;
    std::atomic<uint64_t> fail_closed_count_;  // v4.8.0: Track fail-closed events
};

// =============================================================================
// Global Instance Helper
// =============================================================================
inline MLInferenceEngine& getMLInferenceEngine() {
    static MLInferenceEngine instance;
    return instance;
}

} // namespace ML
} // namespace Chimera
