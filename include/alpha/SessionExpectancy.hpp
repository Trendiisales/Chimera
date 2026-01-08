// ═══════════════════════════════════════════════════════════════════════════════
// include/alpha/SessionExpectancy.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.12: TIME-OF-DAY EXPECTANCY CURVES
//
// PURPOSE: Same signal ≠ same expectancy at different times.
// Institutions never trade evenly through the day.
//
// EXAMPLES:
// - NAS100 at 03:00 UTC → trash
// - NAS100 at NY open  → gold
// - Gold Asia vs NY   → totally different
//
// IMPLEMENTATION:
// - Track expectancy by hour for each symbol
// - Auto-learn from actual trade results
// - Scale edge multiplier by session quality
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <mutex>
#include <fstream>

namespace Chimera {
namespace Alpha {

// ─────────────────────────────────────────────────────────────────────────────
// Hour Expectancy - Per-hour metrics
// ─────────────────────────────────────────────────────────────────────────────
struct HourExpectancy {
    // v4.9.12 HARDENING: Minimum sample sizes for statistical confidence
    // Audit recommendation: ≥30 trades before trusting expectancy
    static constexpr int MIN_TRADES_FOR_EDGE_ADJUSTMENT = 30;
    static constexpr int MIN_TRADES_FOR_DISABLE = 40;
    static constexpr int MIN_TRADES_FOR_BOOST = 50;
    
    int hour = 0;                      // UTC hour (0-23)
    int trades = 0;
    int wins = 0;
    double total_pnl_R = 0.0;          // Total PnL in R multiples
    double expectancy_R = 0.0;         // Average R per trade
    double edge_multiplier = 1.0;      // Applied to signals
    
    double winRate() const {
        return trades > 0 ? static_cast<double>(wins) / trades : 0.0;
    }
    
    // v4.9.12: Returns confidence level based on sample size
    double sampleConfidence() const {
        if (trades < 10) return 0.0;
        if (trades < MIN_TRADES_FOR_EDGE_ADJUSTMENT) return 0.3;
        if (trades < MIN_TRADES_FOR_DISABLE) return 0.6;
        if (trades < MIN_TRADES_FOR_BOOST) return 0.8;
        return 1.0;
    }
    
    void update(bool win, double pnl_R) {
        trades++;
        if (win) wins++;
        total_pnl_R += pnl_R;
        
        // Rolling expectancy with decay
        double alpha = 0.1;  // 10% weight to new data
        expectancy_R = (1.0 - alpha) * expectancy_R + alpha * pnl_R;
        
        // v4.9.12 HARDENING: Require 30+ trades before adjusting edge multiplier
        // This prevents noisy early data from suppressing good regimes
        if (trades >= MIN_TRADES_FOR_EDGE_ADJUSTMENT) {
            // Scale adjustment strength by confidence
            double confidence = sampleConfidence();
            double raw_adjustment = 1.0 + expectancy_R * 0.2;
            edge_multiplier = 1.0 + (raw_adjustment - 1.0) * confidence;
            edge_multiplier = std::clamp(edge_multiplier, 0.3, 1.5);
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Symbol Session Expectancy - 24 hours per symbol
// ─────────────────────────────────────────────────────────────────────────────
class SymbolSessionExpectancy {
public:
    SymbolSessionExpectancy() {
        for (int h = 0; h < 24; h++) {
            hours_[h].hour = h;
            hours_[h].edge_multiplier = getDefaultMultiplier(h);
        }
    }
    
    void setSymbol(const char* symbol) {
        strncpy(symbol_, symbol, 15);
        applySymbolDefaults();
    }
    
    // Record trade result
    void recordTrade(int utc_hour, bool win, double pnl_R) {
        if (utc_hour < 0 || utc_hour > 23) return;
        hours_[utc_hour].update(win, pnl_R);
    }
    
    // Get edge multiplier for hour
    double getEdgeMultiplier(int utc_hour) const {
        if (utc_hour < 0 || utc_hour > 23) return 1.0;
        return hours_[utc_hour].edge_multiplier;
    }
    
    // Get expectancy for hour
    double getExpectancy(int utc_hour) const {
        if (utc_hour < 0 || utc_hour > 23) return 0.0;
        return hours_[utc_hour].expectancy_R;
    }
    
    // Check if hour is tradeable
    bool isHourTradeable(int utc_hour) const {
        if (utc_hour < 0 || utc_hour > 23) return false;
        
        const auto& h = hours_[utc_hour];
        
        // v4.9.12 HARDENING: Require 40+ trades before disabling an hour
        // This prevents noisy early data from killing good sessions
        if (h.trades < HourExpectancy::MIN_TRADES_FOR_DISABLE) {
            return h.edge_multiplier >= 0.5;  // Use default until we have data
        }
        
        // Disable if expectancy is significantly negative WITH sufficient data
        if (h.expectancy_R < -0.3 && h.trades >= HourExpectancy::MIN_TRADES_FOR_DISABLE) {
            return false;
        }
        
        return h.edge_multiplier >= 0.4;
    }
    
    // Get best trading hours
    void getBestHours(int* out_hours, int* out_count, int max_hours = 6) const {
        struct HourScore {
            int hour;
            double score;
        };
        HourScore scores[24];
        
        for (int h = 0; h < 24; h++) {
            scores[h].hour = h;
            scores[h].score = hours_[h].expectancy_R * hours_[h].edge_multiplier;
        }
        
        // Sort by score descending
        for (int i = 0; i < 23; i++) {
            for (int j = i + 1; j < 24; j++) {
                if (scores[j].score > scores[i].score) {
                    std::swap(scores[i], scores[j]);
                }
            }
        }
        
        *out_count = std::min(max_hours, 24);
        for (int i = 0; i < *out_count; i++) {
            out_hours[i] = scores[i].hour;
        }
    }
    
    // Reset to defaults
    void reset() {
        for (int h = 0; h < 24; h++) {
            hours_[h] = HourExpectancy{};
            hours_[h].hour = h;
            hours_[h].edge_multiplier = getDefaultMultiplier(h);
        }
        applySymbolDefaults();
    }
    
    // Accessors
    const char* symbol() const { return symbol_; }
    const HourExpectancy& getHour(int h) const { return hours_[h]; }
    
    // Default multiplier by UTC hour (before learning) - public for use by manager
    static double getDefaultMultiplier(int utc_hour) {
        // London-NY overlap (14:00-16:00 UTC) is best
        if (utc_hour >= 14 && utc_hour < 16) return 1.3;
        
        // London session (08:00-16:00 UTC)
        if (utc_hour >= 8 && utc_hour < 16) return 1.1;
        
        // NY session (14:00-21:00 UTC)
        if (utc_hour >= 16 && utc_hour < 21) return 1.15;
        
        // Asia session (00:00-08:00 UTC)
        if (utc_hour >= 0 && utc_hour < 8) return 0.7;
        
        // Off hours
        return 0.5;
    }
    
private:
    // Apply symbol-specific defaults
    void applySymbolDefaults() {
        // Session quality matters
        if (strstr(symbol_, "BTC") || strstr(symbol_, "ETH") || strstr(symbol_, "SOL")) {
            for (int h = 0; h < 24; h++) {
                // Baseline session quality
                hours_[h].edge_multiplier = std::max(0.75, hours_[h].edge_multiplier);
            }
        }
        
        // Gold: Best during London
        if (strstr(symbol_, "XAU")) {
            for (int h = 8; h < 16; h++) {
                hours_[h].edge_multiplier = 1.2;
            }
            for (int h = 0; h < 5; h++) {
                hours_[h].edge_multiplier = 0.7;
            }
        }
        
        // Indices: Best during home session
        if (strstr(symbol_, "NAS") || strstr(symbol_, "US30")) {
            // NY session boost
            for (int h = 14; h < 21; h++) {
                hours_[h].edge_multiplier = 1.25;
            }
            // Asia is dead for US indices
            for (int h = 0; h < 8; h++) {
                hours_[h].edge_multiplier = 0.5;
            }
        }
    }
    
private:
    char symbol_[16] = {0};
    HourExpectancy hours_[24];
};

// ─────────────────────────────────────────────────────────────────────────────
// Session Expectancy Manager - All symbols
// ─────────────────────────────────────────────────────────────────────────────
class SessionExpectancyManager {
public:
    static constexpr size_t MAX_SYMBOLS = 16;
    
    SymbolSessionExpectancy* get(const char* symbol) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (size_t i = 0; i < count_; i++) {
            if (strcmp(symbols_[i].symbol(), symbol) == 0) {
                return &symbols_[i];
            }
        }
        
        if (count_ < MAX_SYMBOLS) {
            symbols_[count_].setSymbol(symbol);
            return &symbols_[count_++];
        }
        
        return nullptr;
    }
    
    // Record trade for symbol
    void recordTrade(const char* symbol, int utc_hour, bool win, double pnl_R) {
        auto* s = get(symbol);
        if (s) {
            s->recordTrade(utc_hour, win, pnl_R);
        }
    }
    
    // Get edge multiplier for symbol+hour
    double getEdgeMultiplier(const char* symbol, int utc_hour) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (size_t i = 0; i < count_; i++) {
            if (strcmp(symbols_[i].symbol(), symbol) == 0) {
                return symbols_[i].getEdgeMultiplier(utc_hour);
            }
        }
        
        // Default if symbol not found
        return SymbolSessionExpectancy::getDefaultMultiplier(utc_hour);
    }
    
    // Check if symbol+hour is tradeable
    bool isTradeable(const char* symbol, int utc_hour) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (size_t i = 0; i < count_; i++) {
            if (strcmp(symbols_[i].symbol(), symbol) == 0) {
                return symbols_[i].isHourTradeable(utc_hour);
            }
        }
        
        return true;  // Default to tradeable
    }
    
    // Persist to file
    void persist(const char* path = "runtime/session_expectancy.csv") const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::ofstream f(path);
        if (!f.is_open()) return;
        
        f << "SYMBOL,HOUR,TRADES,WINS,WR,EXPECTANCY_R,EDGE_MULT\n";
        
        for (size_t i = 0; i < count_; i++) {
            for (int h = 0; h < 24; h++) {
                const auto& hour = symbols_[i].getHour(h);
                if (hour.trades > 0) {
                    f << symbols_[i].symbol() << ","
                      << h << ","
                      << hour.trades << ","
                      << hour.wins << ","
                      << std::fixed << std::setprecision(3) << hour.winRate() << ","
                      << std::setprecision(3) << hour.expectancy_R << ","
                      << std::setprecision(2) << hour.edge_multiplier << "\n";
                }
            }
        }
    }
    
    // Load from file
    void load(const char* path = "runtime/session_expectancy.csv") {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::ifstream f(path);
        if (!f.is_open()) return;
        
        std::string line;
        std::getline(f, line);  // Skip header
        
        while (std::getline(f, line)) {
            char symbol[16];
            int hour, trades, wins;
            double wr, exp_r, edge_mult;
            
            if (sscanf(line.c_str(), "%15[^,],%d,%d,%d,%lf,%lf,%lf",
                       symbol, &hour, &trades, &wins, &wr, &exp_r, &edge_mult) == 7) {
                auto* s = get(symbol);
                if (s && hour >= 0 && hour < 24) {
                    // Reconstruct from data (simplified)
                    // In real impl, would store/load full state
                }
            }
        }
    }
    
    void resetAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < count_; i++) {
            symbols_[i].reset();
        }
    }
    
private:
    static double getDefaultMultiplier(int utc_hour) {
        // Same as SymbolSessionExpectancy static version
        if (utc_hour >= 14 && utc_hour < 16) return 1.3;
        if (utc_hour >= 8 && utc_hour < 16) return 1.1;
        if (utc_hour >= 16 && utc_hour < 21) return 1.15;
        if (utc_hour >= 0 && utc_hour < 8) return 0.7;
        return 0.5;
    }
    
private:
    mutable std::mutex mutex_;
    SymbolSessionExpectancy symbols_[MAX_SYMBOLS];
    size_t count_ = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Global Manager
// ─────────────────────────────────────────────────────────────────────────────
inline SessionExpectancyManager& getSessionExpectancyManager() {
    static SessionExpectancyManager mgr;
    return mgr;
}

} // namespace Alpha
} // namespace Chimera
