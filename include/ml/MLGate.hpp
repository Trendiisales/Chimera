// =============================================================================
// MLGate.hpp - Chimera ML Execution Gate (v4.6.0)
// =============================================================================
// PURPOSE: ML acts as VETO + SIZE SCALER, not signal generator
// 
// EXECUTION FLOW:
//   Rule Engine proposes trade
//     → MLGate.evaluate() 
//       → Distribution checks (quantiles)
//         → Latency-aware threshold
//           → Session-specific size cap
//             → ACCEPT or REJECT with reason code
//
// ML NEVER FIRES TRADES - IT ONLY VETOES AND SCALES
// =============================================================================
#pragma once

#include "MLModel.hpp"
#include <algorithm>
#include <cmath>
#include <atomic>
#include <cstdio>
#include <chrono>
#include <cstring>

namespace Chimera {
namespace ML {

// =============================================================================
// ML Gate - Distribution-Aware, Latency-Aware Trade Filter
// =============================================================================
class MLGate {
public:
    // Reference latency for penalty calculation (microseconds)
    static constexpr double LATENCY_REF_US = 200.0;
    
    // Global size scaling bounds (session caps applied on top)
    static constexpr double MIN_SIZE_SCALE = 0.25;
    static constexpr double MAX_SIZE_SCALE = 1.50;
    
    // =========================================================================
    // Core Evaluation - CALL THIS FOR EVERY TRADE CANDIDATE
    // =========================================================================
    MLGateResult evaluate(
        const MLQuantiles& q,
        Regime regime,
        Session session,
        double latency_us,
        double base_edge_override = 0.0  // 0 = use session default
    ) {
        MLGateResult result;
        result.quantiles = q;
        result.regime = regime;
        result.session = session;
        result.latency_us = latency_us;
        result.confidence = q.confidence();
        
        // Get session-specific thresholds
        SessionThresholds thresh = SessionThresholds::forSession(session);
        double base_edge = (base_edge_override > 0.0) ? base_edge_override : thresh.min_edge;
        
        // =====================================================================
        // CHECK 1: Minimum IQR (distribution must be meaningful)
        // =====================================================================
        double iqr = q.iqr();
        if (iqr < thresh.min_iqr) {
            result.decision = MLGateDecision::REJECT;
            result.reject_reason = RejectReason::IQR_TOO_NARROW;
            rejects_iqr_.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        
        // =====================================================================
        // CHECK 2: Tail Risk - ABSOLUTE (q10 must not be catastrophic)
        // =====================================================================
        if (q.q10 < -thresh.max_tail_loss) {
            result.decision = MLGateDecision::REJECT;
            result.reject_reason = RejectReason::TAIL_RISK_HIGH;
            rejects_tail_.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        
        // =====================================================================
        // CHECK 3: Tail Risk - RELATIVE (tail spread vs q50)
        // Catches regime stress even when absolute q10 looks OK
        // =====================================================================
        double tail_spread = q.tailSpread();
        if (tail_spread > thresh.tail_spread_max) {
            result.decision = MLGateDecision::REJECT;
            result.reject_reason = RejectReason::TAIL_SPREAD;
            rejects_tail_spread_.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        
        // =====================================================================
        // CHECK 4: Latency-Aware Edge Requirement
        // =====================================================================
        // When latency is bad, require MORE edge to compensate
        result.latency_penalty = latency_us / LATENCY_REF_US;
        result.required_edge = base_edge * (1.0 + result.latency_penalty * 0.5);
        
        if (q.q50 < result.required_edge) {
            result.decision = MLGateDecision::REJECT;
            result.reject_reason = RejectReason::EDGE_LOW;
            rejects_edge_.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        
        // =====================================================================
        // CHECK 5: Latency Hard Block
        // =====================================================================
        if (latency_us > thresh.max_latency_us) {
            result.decision = MLGateDecision::REJECT;
            result.reject_reason = RejectReason::LATENCY_HIGH;
            rejects_latency_.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        
        // =====================================================================
        // CHECK 6: DEAD Regime - Extra Scrutiny
        // =====================================================================
        if (regime == Regime::DEAD) {
            // In DEAD regime, require asymmetric upside
            if (!q.hasAsymmetricUpside(2.0)) {
                result.decision = MLGateDecision::REJECT;
                result.reject_reason = RejectReason::DEAD_REGIME;
                rejects_regime_.fetch_add(1, std::memory_order_relaxed);
                return result;
            }
        }
        
        // =====================================================================
        // PASSED ALL CHECKS - Calculate Size Scale with SESSION CAP
        // =====================================================================
        // Size scales with expectancy relative to required edge
        double expectancy_ratio = q.q50 / result.required_edge;
        double raw_scale = std::clamp(expectancy_ratio, MIN_SIZE_SCALE, MAX_SIZE_SCALE);
        
        // Bonus: If upside is very asymmetric, allow slightly larger size
        if (q.hasAsymmetricUpside(2.0) && regime == Regime::BURST) {
            raw_scale = std::min(raw_scale * 1.2, MAX_SIZE_SCALE);
        }
        
        // Apply SESSION-SPECIFIC SIZE CAP (critical for thin markets)
        result.size_scale = std::min(raw_scale, thresh.max_size_scale);
        
        result.decision = MLGateDecision::ACCEPT;
        result.reject_reason = RejectReason::NONE;
        accepts_.fetch_add(1, std::memory_order_relaxed);
        
        return result;
    }
    
    // =========================================================================
    // Statistics
    // =========================================================================
    struct Stats {
        uint64_t accepts = 0;
        uint64_t rejects_iqr = 0;
        uint64_t rejects_tail = 0;
        uint64_t rejects_tail_spread = 0;
        uint64_t rejects_edge = 0;
        uint64_t rejects_latency = 0;
        uint64_t rejects_regime = 0;
        
        uint64_t total_rejects() const {
            return rejects_iqr + rejects_tail + rejects_tail_spread + 
                   rejects_edge + rejects_latency + rejects_regime;
        }
        
        double accept_rate() const {
            uint64_t total = accepts + total_rejects();
            return total > 0 ? (100.0 * accepts / total) : 0.0;
        }
    };
    
    Stats getStats() const {
        Stats s;
        s.accepts = accepts_.load(std::memory_order_relaxed);
        s.rejects_iqr = rejects_iqr_.load(std::memory_order_relaxed);
        s.rejects_tail = rejects_tail_.load(std::memory_order_relaxed);
        s.rejects_tail_spread = rejects_tail_spread_.load(std::memory_order_relaxed);
        s.rejects_edge = rejects_edge_.load(std::memory_order_relaxed);
        s.rejects_latency = rejects_latency_.load(std::memory_order_relaxed);
        s.rejects_regime = rejects_regime_.load(std::memory_order_relaxed);
        return s;
    }
    
    void printStats() const {
        auto s = getStats();
        std::printf("[MLGate] Stats: accepts=%lu (%.1f%%) | rejects: iqr=%lu tail=%lu tailspread=%lu edge=%lu lat=%lu regime=%lu\n",
            (unsigned long)s.accepts, s.accept_rate(),
            (unsigned long)s.rejects_iqr, (unsigned long)s.rejects_tail,
            (unsigned long)s.rejects_tail_spread, (unsigned long)s.rejects_edge,
            (unsigned long)s.rejects_latency, (unsigned long)s.rejects_regime);
    }
    
    void reset() {
        accepts_.store(0);
        rejects_iqr_.store(0);
        rejects_tail_.store(0);
        rejects_tail_spread_.store(0);
        rejects_edge_.store(0);
        rejects_latency_.store(0);
        rejects_regime_.store(0);
    }
    
private:
    std::atomic<uint64_t> accepts_{0};
    std::atomic<uint64_t> rejects_iqr_{0};
    std::atomic<uint64_t> rejects_tail_{0};
    std::atomic<uint64_t> rejects_tail_spread_{0};
    std::atomic<uint64_t> rejects_edge_{0};
    std::atomic<uint64_t> rejects_latency_{0};
    std::atomic<uint64_t> rejects_regime_{0};
};

// =============================================================================
// Global ML Gate Instance
// =============================================================================
inline MLGate& getMLGate() {
    static MLGate instance;
    return instance;
}

// =============================================================================
// No-Trade Streak Detector (Diagnostic Only)
// =============================================================================
// Warns when ML rejects too many candidates - indicates possible:
//   - Regime misclassification
//   - Feature drift
//   - Broken upstream signal
//
// USAGE:
//   Call record() on EVERY ML candidate, including:
//   - Normal accepts/rejects from MLGate
//   - Early rejects (session block, DEAD regime, drift kill)
//   - Do NOT call for ML-disabled symbols (they bypass ML entirely)
//
//   streak.record(result.accepted());
//   if (streak.shouldWarn()) {
//       printf("⚠️ High reject rate\n");
//   }
//
// CLOCK: Uses steady_clock (monotonic) - consistent with latency measurements
// =============================================================================
class NoTradeStreakDetector {
public:
    struct Config {
        double warning_reject_rate = 0.95;  // Warn if reject rate > 95%
        uint64_t window_seconds = 1800;     // 30 minute window
        uint64_t min_samples = 50;          // Need at least 50 candidates
    };
    
    NoTradeStreakDetector() noexcept : config_{} { reset(); }
    explicit NoTradeStreakDetector(const Config& cfg) noexcept : config_(cfg) { reset(); }
    
    // Call after each ML evaluation (NOT for ML-disabled symbols)
    void record(bool accepted) noexcept {
        auto now = std::chrono::steady_clock::now();
        uint64_t now_s = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        
        // Reset window if too old
        if (now_s - window_start_s_ > config_.window_seconds) {
            window_start_s_ = now_s;
            window_accepts_ = 0;
            window_rejects_ = 0;
        }
        
        if (accepted) {
            window_accepts_++;
            total_accepts_.fetch_add(1, std::memory_order_relaxed);
        } else {
            window_rejects_++;
            total_rejects_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    // Check if we should warn
    bool shouldWarn() const noexcept {
        uint64_t total = window_accepts_ + window_rejects_;
        if (total < config_.min_samples) return false;
        
        double reject_rate = static_cast<double>(window_rejects_) / total;
        return reject_rate > config_.warning_reject_rate;
    }
    
    double windowRejectRate() const noexcept {
        uint64_t total = window_accepts_ + window_rejects_;
        return total > 0 ? static_cast<double>(window_rejects_) / total : 0.0;
    }
    
    void reset() noexcept {
        auto now = std::chrono::steady_clock::now();
        window_start_s_ = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        window_accepts_ = 0;
        window_rejects_ = 0;
    }
    
    void printStatus() const {
        std::printf("[NoTradeStreak] window: %lu accepts, %lu rejects (%.1f%% reject rate)%s\n",
            (unsigned long)window_accepts_, (unsigned long)window_rejects_,
            windowRejectRate() * 100.0,
            shouldWarn() ? " ⚠️ WARNING" : "");
    }
    
private:
    Config config_;
    uint64_t window_start_s_ = 0;
    uint64_t window_accepts_ = 0;
    uint64_t window_rejects_ = 0;
    std::atomic<uint64_t> total_accepts_{0};
    std::atomic<uint64_t> total_rejects_{0};
};

inline NoTradeStreakDetector& getNoTradeStreakDetector() {
    static NoTradeStreakDetector instance;
    return instance;
}

// =============================================================================
// Symbol-Level ML Enable Flag
// =============================================================================
// Allows per-symbol ML control for:
//   - A/B testing (ML on vs ML off)
//   - Isolating problem symbols
//   - Gradual rollout
//
// THREAD SAFETY:
//   - Configure at startup ONLY (before trading loop)
//   - isEnabled() is safe to call from hot path (read-only)
//   - setEnabled() is NOT thread-safe, use only during configuration
//
// USAGE:
//   auto& ml_enable = getSymbolMLEnable();
//   if (!ml_enable.isEnabled(symbol)) {
//       return MLGateResult::bypass(regime, session);  // ← IMPORTANT
//   }
//   // ... continue with ML evaluation
// =============================================================================
class SymbolMLEnable {
public:
    static constexpr size_t MAX_SYMBOLS = 32;
    
    SymbolMLEnable() noexcept {
        for (size_t i = 0; i < MAX_SYMBOLS; ++i) {
            symbols_[i][0] = '\0';
            enabled_[i] = true;  // Default: ML enabled
        }
        symbol_count_ = 0;
    }
    
    // Check if ML is enabled for symbol (returns true if not registered)
    // THREAD-SAFE: Read-only, safe to call from hot path
    bool isEnabled(const char* symbol) const noexcept {
        for (size_t i = 0; i < symbol_count_; ++i) {
            if (std::strcmp(symbols_[i], symbol) == 0) {
                return enabled_[i];
            }
        }
        return true;  // Default enabled
    }
    
    // Set ML enabled/disabled for symbol
    // NOT THREAD-SAFE: Call only during configuration, before trading
    void setEnabled(const char* symbol, bool enabled) noexcept {
        // Find existing
        for (size_t i = 0; i < symbol_count_; ++i) {
            if (std::strcmp(symbols_[i], symbol) == 0) {
                enabled_[i] = enabled;
                std::printf("[SymbolMLEnable] %s ML %s\n", symbol, enabled ? "ENABLED" : "DISABLED");
                return;
            }
        }
        
        // Add new
        if (symbol_count_ < MAX_SYMBOLS) {
            std::strncpy(symbols_[symbol_count_], symbol, 15);
            symbols_[symbol_count_][15] = '\0';
            enabled_[symbol_count_] = enabled;
            symbol_count_++;
            std::printf("[SymbolMLEnable] %s ML %s (registered)\n", symbol, enabled ? "ENABLED" : "DISABLED");
        }
    }
    
    // Convenience methods
    void enable(const char* symbol) noexcept { setEnabled(symbol, true); }
    void disable(const char* symbol) noexcept { setEnabled(symbol, false); }
    
    // Enable/disable all
    void enableAll() noexcept {
        for (size_t i = 0; i < symbol_count_; ++i) enabled_[i] = true;
        std::printf("[SymbolMLEnable] All symbols ML ENABLED\n");
    }
    
    void disableAll() noexcept {
        for (size_t i = 0; i < symbol_count_; ++i) enabled_[i] = false;
        std::printf("[SymbolMLEnable] All symbols ML DISABLED\n");
    }
    
    void printStatus() const {
        std::printf("[SymbolMLEnable] %zu symbols registered:\n", symbol_count_);
        for (size_t i = 0; i < symbol_count_; ++i) {
            std::printf("  %s: %s\n", symbols_[i], enabled_[i] ? "ENABLED" : "DISABLED");
        }
    }
    
private:
    char symbols_[MAX_SYMBOLS][16];
    bool enabled_[MAX_SYMBOLS];
    size_t symbol_count_;
};

inline SymbolMLEnable& getSymbolMLEnable() {
    static SymbolMLEnable instance;
    return instance;
}

} // namespace ML
} // namespace Chimera
