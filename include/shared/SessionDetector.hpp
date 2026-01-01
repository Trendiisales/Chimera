// =============================================================================
// SessionDetector.hpp - v4.7.0 - NY EXPANSION DETECTION
// =============================================================================
// PURPOSE: Detect when NY session actually starts moving (not just clock-based)
//
// Clock time alone is insufficient. Chimera must detect when NY actually
// starts moving based on:
//   1. Volatility expansion
//   2. Volume participation
//   3. Directional persistence
//
// This prevents trading dead NY opens and pre-NY teasing.
//
// OWNERSHIP: Jo
// LAST VERIFIED: 2025-01-01
// =============================================================================
#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>
#include <chrono>
#include <unordered_map>
#include <string>
#include "SymbolPolicy.hpp"

namespace Chimera {

// =============================================================================
// Expansion Metrics (per-symbol)
// =============================================================================
struct ExpansionMetrics {
    double atr_1m = 0.0;       // 1-minute ATR
    double atr_5m = 0.0;       // 5-minute ATR (baseline)
    double vol_ratio = 0.0;   // Current volume / baseline volume
    double range_break = 0.0; // % of prior range breached
    uint64_t ts_ns = 0;
    
    void clear() {
        atr_1m = 0.0;
        atr_5m = 0.0;
        vol_ratio = 0.0;
        range_break = 0.0;
        ts_ns = 0;
    }
};

// =============================================================================
// Session State
// =============================================================================
struct SessionState {
    SessionWindow current_window = SessionWindow::ANY;
    bool ny_expansion_active = false;
    bool london_expansion_active = false;
    uint64_t expansion_start_ts = 0;
    uint64_t last_update_ts = 0;
    
    // For standby detection
    uint64_t no_edge_since_ts = 0;
    bool should_standby = false;
};

// =============================================================================
// Session Detector (per-symbol tracking)
// =============================================================================
class SessionDetector {
public:
    // Expansion detection thresholds
    struct Config {
        // Volatility expansion
        double atr_expansion_ratio = 1.5;  // 1m ATR must be 1.5x 5m ATR
        
        // Volume participation
        double vol_expansion_ratio = 1.8;  // Volume must be 1.8x baseline
        
        // Range break
        double range_break_pct = 0.6;      // Must break 60% of prior range
        
        // Time windows (UTC)
        // NY: 13:30-20:00 UTC (09:30-16:00 EST)
        // London: 07:00-16:00 UTC
        // Asia: 00:00-07:00 UTC
        int ny_start_hour = 13;
        int ny_start_min = 30;
        int ny_end_hour = 20;
        int london_start_hour = 7;
        int london_end_hour = 16;
        int asia_start_hour = 0;
        int asia_end_hour = 7;
        
        // Standby detection
        uint64_t no_edge_standby_ns = 30ULL * 60 * 1000000000ULL; // 30 minutes
    };
    
    SessionDetector() = default;
    
    void setConfig(const Config& cfg) { config_ = cfg; }
    const Config& config() const { return config_; }
    
    // =========================================================================
    // UPDATE SESSION STATE
    // =========================================================================
    SessionState updateSession(uint64_t now_ns) {
        state_.last_update_ts = now_ns;
        
        // Get current UTC time
        auto now_tp = std::chrono::system_clock::now();
        auto now_time = std::chrono::system_clock::to_time_t(now_tp);
        std::tm* utc_tm = std::gmtime(&now_time);
        
        int hour = utc_tm->tm_hour;
        int min = utc_tm->tm_min;
        
        // Determine session window
        SessionWindow prev_window = state_.current_window;
        
        if (isInNYSession(hour, min)) {
            state_.current_window = SessionWindow::NY;
        } else if (isInLondonSession(hour)) {
            state_.current_window = SessionWindow::LONDON;
        } else if (isInAsiaSession(hour)) {
            state_.current_window = SessionWindow::ASIA;
        } else {
            state_.current_window = SessionWindow::ANY;
        }
        
        // Check for London-NY overlap
        if (isInNYSession(hour, min) && isInLondonSession(hour)) {
            state_.current_window = SessionWindow::LONDON_NY;
        }
        
        // Log session changes
        if (prev_window != state_.current_window) {
            printf("[SESSION] Window changed: %s → %s\n",
                   session_window_str(prev_window),
                   session_window_str(state_.current_window));
        }
        
        return state_;
    }
    
    // =========================================================================
    // CHECK NY EXPANSION (real detection, not clock-based)
    // =========================================================================
    [[nodiscard]] bool isNYExpansion(const ExpansionMetrics& m) const {
        // Must be in NY session first
        if (state_.current_window != SessionWindow::NY &&
            state_.current_window != SessionWindow::LONDON_NY) {
            return false;
        }
        
        // Volatility expansion check
        if (m.atr_5m <= 0.0) return false;  // Need baseline
        if (m.atr_1m < config_.atr_expansion_ratio * m.atr_5m) {
            return false;
        }
        
        // Volume participation check
        if (m.vol_ratio < config_.vol_expansion_ratio) {
            return false;
        }
        
        // Range break check
        if (m.range_break < config_.range_break_pct) {
            return false;
        }
        
        return true;
    }
    
    // =========================================================================
    // UPDATE SYMBOL EXPANSION METRICS
    // =========================================================================
    void updateMetrics(
        const char* symbol,
        double price,
        double bid_size,
        double ask_size,
        uint64_t now_ns
    ) {
        auto& sm = symbol_metrics_[symbol];
        
        // Initialize if first tick
        if (sm.tick_count == 0) {
            sm.first_price = price;
            sm.high = price;
            sm.low = price;
            sm.baseline_vol = (bid_size + ask_size) / 2.0;
        }
        
        sm.tick_count++;
        sm.last_price = price;
        sm.high = std::max(sm.high, price);
        sm.low = std::min(sm.low, price);
        
        // Update TR (True Range)
        double tr = sm.high - sm.low;
        if (sm.prev_close > 0.0) {
            tr = std::max(tr, std::abs(sm.high - sm.prev_close));
            tr = std::max(tr, std::abs(sm.low - sm.prev_close));
        }
        
        // EMA of TR for ATR (fast and slow)
        const double alpha_fast = 0.2;  // ~5 period
        const double alpha_slow = 0.05; // ~20 period
        
        sm.atr_fast = sm.atr_fast > 0.0 
            ? alpha_fast * tr + (1.0 - alpha_fast) * sm.atr_fast
            : tr;
        sm.atr_slow = sm.atr_slow > 0.0
            ? alpha_slow * tr + (1.0 - alpha_slow) * sm.atr_slow
            : tr;
        
        // Volume EMA
        double current_vol = (bid_size + ask_size) / 2.0;
        sm.vol_ema = sm.vol_ema > 0.0
            ? alpha_fast * current_vol + (1.0 - alpha_fast) * sm.vol_ema
            : current_vol;
        
        // Calculate expansion metrics
        sm.metrics.atr_1m = sm.atr_fast;
        sm.metrics.atr_5m = sm.atr_slow;
        sm.metrics.vol_ratio = sm.baseline_vol > 0.0 
            ? sm.vol_ema / sm.baseline_vol 
            : 1.0;
        
        // Range break (vs prior period)
        double prior_range = sm.prior_high - sm.prior_low;
        if (prior_range > 0.0) {
            double break_high = std::max(0.0, sm.high - sm.prior_high);
            double break_low = std::max(0.0, sm.prior_low - sm.low);
            sm.metrics.range_break = (break_high + break_low) / prior_range;
        }
        
        sm.metrics.ts_ns = now_ns;
        
        // Check for NY expansion on this symbol
        bool was_expanded = sm.ny_expansion;
        sm.ny_expansion = isNYExpansion(sm.metrics);
        
        if (sm.ny_expansion && !was_expanded) {
            printf("[SESSION] NY EXPANSION DETECTED: %s (ATR ratio=%.2f vol=%.2f range=%.2f)\n",
                   symbol, 
                   sm.atr_fast / std::max(0.0001, sm.atr_slow),
                   sm.metrics.vol_ratio,
                   sm.metrics.range_break);
            sm.expansion_start_ts = now_ns;
        }
    }
    
    // =========================================================================
    // RESET PERIOD (call at period boundaries)
    // =========================================================================
    void resetPeriod(const char* symbol) {
        auto it = symbol_metrics_.find(symbol);
        if (it == symbol_metrics_.end()) return;
        
        auto& sm = it->second;
        
        // Save current range as prior
        sm.prior_high = sm.high;
        sm.prior_low = sm.low;
        sm.prev_close = sm.last_price;
        
        // Reset current period
        sm.high = sm.last_price;
        sm.low = sm.last_price;
        sm.first_price = sm.last_price;
        sm.tick_count = 0;
        
        // Update baseline volume
        if (sm.vol_ema > 0.0) {
            sm.baseline_vol = sm.vol_ema;
        }
    }
    
    // =========================================================================
    // GET EXPANSION METRICS
    // =========================================================================
    [[nodiscard]] const ExpansionMetrics* getMetrics(const char* symbol) const {
        auto it = symbol_metrics_.find(symbol);
        if (it == symbol_metrics_.end()) return nullptr;
        return &it->second.metrics;
    }
    
    [[nodiscard]] bool isSymbolExpanded(const char* symbol) const {
        auto it = symbol_metrics_.find(symbol);
        if (it == symbol_metrics_.end()) return false;
        return it->second.ny_expansion;
    }
    
    // =========================================================================
    // STANDBY DETECTION
    // =========================================================================
    void recordEdge(const char* symbol, double edge) {
        if (edge > 0.3) {  // Meaningful edge
            edge_last_seen_[symbol] = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }
    }
    
    [[nodiscard]] bool shouldStandby(uint64_t now_ns) const {
        // Check if we're in a core session
        if (state_.current_window == SessionWindow::ANY) {
            return true;  // Always standby outside core sessions
        }
        
        // Check if no edge for extended period
        bool any_recent_edge = false;
        for (const auto& [symbol, ts] : edge_last_seen_) {
            if (now_ns - ts < config_.no_edge_standby_ns) {
                any_recent_edge = true;
                break;
            }
        }
        
        if (!any_recent_edge && !edge_last_seen_.empty()) {
            return true;  // No edge for 30+ minutes
        }
        
        return false;
    }
    
    // =========================================================================
    // SESSION GETTERS
    // =========================================================================
    [[nodiscard]] SessionWindow currentWindow() const { return state_.current_window; }
    [[nodiscard]] bool isNYSession() const {
        return state_.current_window == SessionWindow::NY ||
               state_.current_window == SessionWindow::LONDON_NY ||
               state_.current_window == SessionWindow::NY_EXPANSION;
    }
    [[nodiscard]] bool isLondonSession() const {
        return state_.current_window == SessionWindow::LONDON ||
               state_.current_window == SessionWindow::LONDON_NY;
    }
    [[nodiscard]] bool isAsiaSession() const {
        return state_.current_window == SessionWindow::ASIA;
    }
    [[nodiscard]] bool isCoreSession() const {
        return state_.current_window != SessionWindow::ANY;
    }
    
    // =========================================================================
    // DIAGNOSTICS
    // =========================================================================
    void printStatus() const {
        printf("[SESSION] Status:\n");
        printf("  Current window: %s\n", session_window_str(state_.current_window));
        printf("  Symbols tracked: %zu\n", symbol_metrics_.size());
        
        for (const auto& [symbol, sm] : symbol_metrics_) {
            printf("  %s: expanded=%s ATR_ratio=%.2f vol_ratio=%.2f ticks=%lu\n",
                   symbol.c_str(),
                   sm.ny_expansion ? "YES" : "NO",
                   sm.atr_slow > 0.0 ? sm.atr_fast / sm.atr_slow : 0.0,
                   sm.metrics.vol_ratio,
                   (unsigned long)sm.tick_count);
        }
    }

private:
    // Per-symbol metrics tracking
    struct SymbolMetrics {
        ExpansionMetrics metrics;
        double first_price = 0.0;
        double last_price = 0.0;
        double high = 0.0;
        double low = 0.0;
        double prior_high = 0.0;
        double prior_low = 0.0;
        double prev_close = 0.0;
        double atr_fast = 0.0;
        double atr_slow = 0.0;
        double vol_ema = 0.0;
        double baseline_vol = 0.0;
        uint64_t tick_count = 0;
        bool ny_expansion = false;
        uint64_t expansion_start_ts = 0;
    };
    
    Config config_;
    SessionState state_;
    std::unordered_map<std::string, SymbolMetrics> symbol_metrics_;
    std::unordered_map<std::string, uint64_t> edge_last_seen_;
    
    bool isInNYSession(int hour, int min) const {
        int time_mins = hour * 60 + min;
        int ny_start = config_.ny_start_hour * 60 + config_.ny_start_min;
        int ny_end = config_.ny_end_hour * 60;
        return time_mins >= ny_start && time_mins < ny_end;
    }
    
    bool isInLondonSession(int hour) const {
        return hour >= config_.london_start_hour && hour < config_.london_end_hour;
    }
    
    bool isInAsiaSession(int hour) const {
        return hour >= config_.asia_start_hour && hour < config_.asia_end_hour;
    }
};

// =============================================================================
// GLOBAL SESSION DETECTOR ACCESS
// =============================================================================
inline SessionDetector& getSessionDetector() {
    static SessionDetector instance;
    return instance;
}

} // namespace Chimera
