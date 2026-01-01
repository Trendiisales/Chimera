// ═══════════════════════════════════════════════════════════════════════════════
// include/risk/SymbolHealth.hpp
// v4.2.2: Auto-disable/enable symbols based on health metrics
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>

namespace Chimera {

// ─────────────────────────────────────────────────────────────────────────────
// Snapshot for returning health data (copyable)
// ─────────────────────────────────────────────────────────────────────────────
struct SymbolHealthSnapshot {
    int trades = 0;
    int wins = 0;
    double pnl = 0.0;
    bool manually_disabled = false;
    
    double winrate() const {
        return trades > 0 ? double(wins) / trades : 0.0;
    }
    
    bool unhealthy() const {
        return trades >= 10 && winrate() < 0.35;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-symbol health tracking (thread-safe atomics)
// ─────────────────────────────────────────────────────────────────────────────
struct SymbolHealth {
    std::atomic<int> trades{0};
    std::atomic<int> wins{0};
    std::atomic<double> pnl{0.0};
    std::atomic<bool> manually_disabled{false};
    
    double winrate() const {
        int t = trades.load();
        return t > 0 ? double(wins.load()) / t : 0.0;
    }
    
    bool unhealthy() const {
        // Auto-disable if: 10+ trades AND win rate < 35%
        return trades.load() >= 10 && winrate() < 0.35;
    }
    
    void record(bool win, double trade_pnl) {
        trades++;
        if (win) wins++;
        pnl.store(pnl.load() + trade_pnl);
    }
    
    void reset() {
        trades = 0;
        wins = 0;
        pnl = 0.0;
    }
    
    SymbolHealthSnapshot snapshot() const {
        return {
            trades.load(),
            wins.load(),
            pnl.load(),
            manually_disabled.load()
        };
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Global symbol health manager (thread-safe)
// ─────────────────────────────────────────────────────────────────────────────
class SymbolHealthManager {
public:
    static SymbolHealthManager& instance() {
        static SymbolHealthManager inst;
        return inst;
    }
    
    void record_trade(const std::string& symbol, bool win, double pnl) {
        std::lock_guard<std::mutex> lock(mutex_);
        health_[symbol].record(win, pnl);
    }
    
    bool symbol_enabled(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = health_.find(symbol);
        if (it == health_.end())
            return true;  // Unknown symbols default to enabled
        
        if (it->second.manually_disabled.load())
            return false;
            
        return !it->second.unhealthy();
    }
    
    void disable_symbol(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mutex_);
        health_[symbol].manually_disabled = true;
    }
    
    void enable_symbol(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mutex_);
        health_[symbol].manually_disabled = false;
    }
    
    // Nightly reset for fresh statistics
    void nightly_reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [sym, h] : health_) {
            h.reset();
        }
    }
    
    // Get health snapshot for logging/GUI
    SymbolHealthSnapshot get_health(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = health_.find(symbol);
        if (it == health_.end())
            return SymbolHealthSnapshot{};
        return it->second.snapshot();
    }
    
    // Get all symbol stats for dashboard
    std::unordered_map<std::string, SymbolHealthSnapshot> all_health() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::unordered_map<std::string, SymbolHealthSnapshot> result;
        for (const auto& [sym, h] : health_) {
            result[sym] = h.snapshot();
        }
        return result;
    }

private:
    SymbolHealthManager() = default;
    
    mutable std::mutex mutex_;
    std::unordered_map<std::string, SymbolHealth> health_;
};

// Convenience functions
inline void record_trade(const std::string& symbol, bool win, double pnl) {
    SymbolHealthManager::instance().record_trade(symbol, win, pnl);
}

inline bool symbol_enabled(const std::string& symbol) {
    return SymbolHealthManager::instance().symbol_enabled(symbol);
}

} // namespace Chimera
