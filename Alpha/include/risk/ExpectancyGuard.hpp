// ═══════════════════════════════════════════════════════════════════════════════
// Alpha Trading System - Expectancy Guard
// ═══════════════════════════════════════════════════════════════════════════════
// NON-NEGOTIABLE FOR REAL MONEY
// The engine fires itself if needed
// No emotional overrides. No capital bleed.
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <iomanip>
#include <ctime>

namespace Alpha {

// ═══════════════════════════════════════════════════════════════════════════════
// SYMBOL STATISTICS
// ═══════════════════════════════════════════════════════════════════════════════

struct SymbolStats {
    int trades = 0;
    double total_r = 0.0;
    int wins = 0;
    int losses = 0;
    double max_drawdown_r = 0.0;
    double current_drawdown_r = 0.0;
    double peak_r = 0.0;
    uint64_t total_hold_ms = 0;
    int scaled_trades = 0;
    
    // Computed metrics
    double expectancy() const {
        return trades > 0 ? total_r / trades : 0.0;
    }
    
    double win_rate() const {
        return trades > 0 ? static_cast<double>(wins) / trades : 0.0;
    }
    
    double avg_hold_ms() const {
        return trades > 0 ? static_cast<double>(total_hold_ms) / trades : 0.0;
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// EXPECTANCY THRESHOLDS
// ═══════════════════════════════════════════════════════════════════════════════

constexpr int MIN_TRADES_FOR_DISABLE = 30;           // Need 30 trades before disabling
constexpr double DISABLE_EXPECTANCY = -0.05;         // Disable if expectancy < -0.05R
constexpr double DISABLE_WIN_RATE = 0.30;            // Disable if win rate < 30%
constexpr double DISABLE_MAX_DRAWDOWN = -3.0;        // Disable if max DD > 3R
constexpr double WARNING_EXPECTANCY = 0.02;          // Warn if expectancy < 0.02R
constexpr double WARNING_WIN_RATE = 0.40;            // Warn if win rate < 40%

// ═══════════════════════════════════════════════════════════════════════════════
// DISABLE DECISION LOGIC
// ═══════════════════════════════════════════════════════════════════════════════

inline bool should_disable(const SymbolStats& s) {
    // Need minimum sample size
    if (s.trades < MIN_TRADES_FOR_DISABLE)
        return false;
    
    // Negative expectancy = DISABLE
    if (s.expectancy() < DISABLE_EXPECTANCY)
        return true;
    
    // Very low win rate = DISABLE
    if (s.win_rate() < DISABLE_WIN_RATE)
        return true;
    
    // Severe drawdown = DISABLE
    if (s.max_drawdown_r < DISABLE_MAX_DRAWDOWN)
        return true;
    
    return false;
}

inline bool should_warn(const SymbolStats& s) {
    if (s.trades < 10)
        return false;
    
    if (s.expectancy() < WARNING_EXPECTANCY)
        return true;
    
    if (s.win_rate() < WARNING_WIN_RATE)
        return true;
    
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
// EXPECTANCY TRACKER (SINGLETON)
// ═══════════════════════════════════════════════════════════════════════════════

class ExpectancyTracker {
public:
    static ExpectancyTracker& instance() {
        static ExpectancyTracker tracker;
        return tracker;
    }
    
    void record_trade(const std::string& symbol, double r, uint64_t hold_ms, bool scaled) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto& s = stats_[symbol];
        s.trades++;
        s.total_r += r;
        s.total_hold_ms += hold_ms;
        
        if (r >= 0) s.wins++;
        else s.losses++;
        
        if (scaled) s.scaled_trades++;
        
        // Update drawdown tracking
        s.peak_r = std::max(s.peak_r, s.total_r);
        s.current_drawdown_r = s.total_r - s.peak_r;
        s.max_drawdown_r = std::min(s.max_drawdown_r, s.current_drawdown_r);
        
        // Log if warning/disable threshold hit
        if (should_disable(s)) {
            log_disable(symbol, s);
        } else if (should_warn(s)) {
            log_warning(symbol, s);
        }
    }
    
    bool symbol_enabled(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = stats_.find(symbol);
        if (it == stats_.end())
            return true;  // No data = enabled
        
        return !should_disable(it->second);
    }
    
    SymbolStats get_stats(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = stats_.find(symbol);
        if (it == stats_.end())
            return {};
        
        return it->second;
    }
    
    void reset_symbol(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.erase(symbol);
    }
    
    void reset_all() {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.clear();
    }

private:
    ExpectancyTracker() = default;
    
    void log_disable(const std::string& symbol, const SymbolStats& s) {
        std::cerr << "[EXPECTANCY] ❌ DISABLED " << symbol 
                  << " | trades=" << s.trades
                  << " expectancy=" << std::fixed << std::setprecision(3) << s.expectancy()
                  << "R win_rate=" << std::setprecision(1) << (s.win_rate() * 100) << "%"
                  << " max_dd=" << std::setprecision(2) << s.max_drawdown_r << "R\n";
    }
    
    void log_warning(const std::string& symbol, const SymbolStats& s) {
        std::cerr << "[EXPECTANCY] ⚠️ WARNING " << symbol 
                  << " | trades=" << s.trades
                  << " expectancy=" << std::fixed << std::setprecision(3) << s.expectancy()
                  << "R win_rate=" << std::setprecision(1) << (s.win_rate() * 100) << "%\n";
    }
    
    mutable std::mutex mutex_;
    std::unordered_map<std::string, SymbolStats> stats_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// CONVENIENCE FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════════

inline void record_trade(const std::string& symbol, double r, uint64_t hold_ms, bool scaled = false) {
    ExpectancyTracker::instance().record_trade(symbol, r, hold_ms, scaled);
}

inline bool symbol_enabled(const std::string& symbol) {
    return ExpectancyTracker::instance().symbol_enabled(symbol);
}

}  // namespace Alpha
