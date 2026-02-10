// =============================================================================
// MLMetricsPublisher.hpp - Live ML Dashboard Metrics (v4.6.0)
// =============================================================================
// PURPOSE: Export ML metrics for live dashboard / Prometheus
// 
// EXPORTED METRICS (per symbol):
//   - ml_q10, ml_q25, ml_q50, ml_q75, ml_q90
//   - ml_confidence = (q75 - q25) / abs(q50)  ← NEW: Shows ML uncertainty
//   - ml_latency_us
//   - ml_size_scale
//   - ml_decision (0=reject, 1=accept)
//   - ml_regime
//   - ml_session
//
// You now see ML edge LIVE, not in hindsight.
// =============================================================================
#pragma once

#include "MLModel.hpp"
#include "MLGate.hpp"
#include <cstdint>
#include <cstring>
#include <atomic>
#include <mutex>
#include <string>
#include <cstdio>
#include <array>

namespace Chimera {
namespace ML {

// =============================================================================
// Per-Symbol ML Metrics
// =============================================================================
struct MLSymbolMetrics {
    // Quantiles (latest)
    double q10 = 0.0;
    double q25 = 0.0;
    double q50 = 0.0;
    double q75 = 0.0;
    double q90 = 0.0;
    
    // Confidence metric: (q75 - q25) / abs(q50)
    // Shows when ML is uncertain - higher is more confident
    double confidence = 0.0;
    
    // ML Health scalar: single glance system status
    // Combines expectancy (q50) with confidence, range [-5, +5]
    double health = 0.0;
    
    // Execution context
    double latency_us = 0.0;
    double size_scale = 1.0;
    double spread_bps = 0.0;
    
    // Decision
    uint8_t decision = 0;       // 0=reject, 1=accept
    Regime regime = Regime::DEAD;
    Session session = Session::ASIA;
    RejectReason reject_reason = RejectReason::NONE;
    
    // Rolling stats
    double rolling_q50 = 0.0;
    double rolling_accept_rate = 0.0;
    uint64_t total_evaluations = 0;
    uint64_t total_accepts = 0;
    
    // Timestamp
    uint64_t last_update_ns = 0;
    
    // Default constructor with proper initialization
    MLSymbolMetrics() = default;
};

// =============================================================================
// ML Metrics Publisher
// =============================================================================
class MLMetricsPublisher {
public:
    static constexpr size_t MAX_SYMBOLS = 32;
    
    MLMetricsPublisher() noexcept : symbol_count_(0) {
        // Initialize symbol names to empty strings
        for (size_t i = 0; i < MAX_SYMBOLS; ++i) {
            symbols_[i][0] = '\0';
        }
        // MLSymbolMetrics uses default member initializers, no memset needed
    }
    
    // =========================================================================
    // Update Metrics (call after each ML evaluation)
    // =========================================================================
    void update(
        const char* symbol,
        const MLQuantiles& q,
        const MLGateResult& result,
        double latency_us,
        double spread_bps = 0.0
    ) noexcept {
        size_t idx = getOrCreateSymbolIndex(symbol);
        if (idx >= MAX_SYMBOLS) return;
        
        auto& m = metrics_[idx];
        
        // Update quantiles
        m.q10 = q.q10;
        m.q25 = q.q25;
        m.q50 = q.q50;
        m.q75 = q.q75;
        m.q90 = q.q90;
        
        // Update confidence metric
        m.confidence = q.confidence();
        
        // Update health scalar (single glance system status)
        m.health = q.health();
        
        // Update context
        m.latency_us = latency_us;
        m.size_scale = result.size_scale;
        m.spread_bps = spread_bps;
        
        // Update decision
        m.decision = result.accepted() ? 1 : 0;
        m.regime = result.regime;
        m.session = result.session;
        m.reject_reason = result.reject_reason;
        
        // Update rolling stats (EMA)
        double alpha = 0.02;
        m.rolling_q50 = m.rolling_q50 * (1.0 - alpha) + q.q50 * alpha;
        
        m.total_evaluations++;
        if (result.accepted()) m.total_accepts++;
        m.rolling_accept_rate = (double)m.total_accepts / m.total_evaluations;
        
        // Timestamp
        auto now = std::chrono::steady_clock::now();
        m.last_update_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
    }
    
    // =========================================================================
    // Get Metrics
    // =========================================================================
    bool getMetrics(const char* symbol, MLSymbolMetrics& out) const noexcept {
        size_t idx = findSymbolIndex(symbol);
        if (idx >= MAX_SYMBOLS) return false;
        out = metrics_[idx];
        return true;
    }
    
    // =========================================================================
    // Export to JSON (for WebSocket broadcast)
    // =========================================================================
    std::string toJSON() const {
        std::string json = "{\"ml_metrics\":{";
        bool first = true;
        
        for (size_t i = 0; i < symbol_count_; ++i) {
            if (!first) json += ",";
            first = false;
            
            const auto& m = metrics_[i];
            char buf[700];
            std::snprintf(buf, sizeof(buf),
                "\"%s\":{"
                "\"q10\":%.4f,\"q25\":%.4f,\"q50\":%.4f,\"q75\":%.4f,\"q90\":%.4f,"
                "\"confidence\":%.4f,\"health\":%.4f,"
                "\"latency_us\":%.2f,\"size_scale\":%.3f,\"spread_bps\":%.2f,"
                "\"decision\":%d,\"reject_reason\":\"%s\","
                "\"regime\":\"%s\",\"session\":\"%s\","
                "\"rolling_q50\":%.4f,\"accept_rate\":%.3f,"
                "\"evaluations\":%lu}",
                symbols_[i],
                m.q10, m.q25, m.q50, m.q75, m.q90,
                m.confidence, m.health,
                m.latency_us, m.size_scale, m.spread_bps,
                m.decision, rejectReasonToStr(m.reject_reason),
                regimeToStr(m.regime), sessionToStr(m.session),
                m.rolling_q50, m.rolling_accept_rate,
                (unsigned long)m.total_evaluations);
            
            json += buf;
        }
        
        json += "}}";
        return json;
    }
    
    // =========================================================================
    // Export to Prometheus format
    // =========================================================================
    std::string toPrometheus() const {
        std::string out;
        
        for (size_t i = 0; i < symbol_count_; ++i) {
            const auto& m = metrics_[i];
            const char* sym = symbols_[i];
            char buf[256];
            
            std::snprintf(buf, sizeof(buf), "ml_q50{symbol=\"%s\"} %.4f\n", sym, m.q50);
            out += buf;
            
            std::snprintf(buf, sizeof(buf), "ml_q10{symbol=\"%s\"} %.4f\n", sym, m.q10);
            out += buf;
            
            std::snprintf(buf, sizeof(buf), "ml_q90{symbol=\"%s\"} %.4f\n", sym, m.q90);
            out += buf;
            
            // NEW: Confidence metric
            std::snprintf(buf, sizeof(buf), "ml_confidence{symbol=\"%s\"} %.4f\n", sym, m.confidence);
            out += buf;
            
            // NEW: Health scalar (single glance system status)
            std::snprintf(buf, sizeof(buf), "ml_health{symbol=\"%s\"} %.4f\n", sym, m.health);
            out += buf;
            
            std::snprintf(buf, sizeof(buf), "ml_latency_us{symbol=\"%s\"} %.2f\n", sym, m.latency_us);
            out += buf;
            
            std::snprintf(buf, sizeof(buf), "ml_size_scale{symbol=\"%s\"} %.3f\n", sym, m.size_scale);
            out += buf;
            
            std::snprintf(buf, sizeof(buf), "ml_decision{symbol=\"%s\"} %d\n", sym, m.decision);
            out += buf;
            
            std::snprintf(buf, sizeof(buf), "ml_accept_rate{symbol=\"%s\"} %.4f\n", sym, m.rolling_accept_rate);
            out += buf;
            
            std::snprintf(buf, sizeof(buf), "ml_evaluations{symbol=\"%s\"} %lu\n", sym, (unsigned long)m.total_evaluations);
            out += buf;
        }
        
        return out;
    }
    
    // =========================================================================
    // Print Summary
    // =========================================================================
    void printSummary() const {
        std::printf("[MLMetrics] Symbols tracked: %zu\n", symbol_count_);
        
        for (size_t i = 0; i < symbol_count_; ++i) {
            const auto& m = metrics_[i];
            std::printf("  %s: q50=%.3f conf=%.2f health=%.2f lat=%.0fus scale=%.2f accept=%.1f%% evals=%lu\n",
                symbols_[i], m.q50, m.confidence, m.health, m.latency_us, m.size_scale,
                m.rolling_accept_rate * 100.0, (unsigned long)m.total_evaluations);
        }
    }
    
    size_t symbolCount() const { return symbol_count_; }
    
private:
    size_t getOrCreateSymbolIndex(const char* symbol) noexcept {
        // Find existing
        for (size_t i = 0; i < symbol_count_; ++i) {
            if (std::strcmp(symbols_[i], symbol) == 0) return i;
        }
        
        // Create new
        if (symbol_count_ >= MAX_SYMBOLS) return MAX_SYMBOLS;
        
        std::strncpy(symbols_[symbol_count_], symbol, 15);
        symbols_[symbol_count_][15] = '\0';
        return symbol_count_++;
    }
    
    size_t findSymbolIndex(const char* symbol) const noexcept {
        for (size_t i = 0; i < symbol_count_; ++i) {
            if (std::strcmp(symbols_[i], symbol) == 0) return i;
        }
        return MAX_SYMBOLS;
    }
    
private:
    char symbols_[MAX_SYMBOLS][16];
    std::array<MLSymbolMetrics, MAX_SYMBOLS> metrics_;
    size_t symbol_count_;
};

// =============================================================================
// Global Metrics Publisher
// =============================================================================
inline MLMetricsPublisher& getMLMetricsPublisher() {
    static MLMetricsPublisher instance;
    return instance;
}

} // namespace ML
} // namespace Chimera
