// ═══════════════════════════════════════════════════════════════════════════════
// include/symbol/SymbolScorecard.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.11: SELF-PRUNING SYMBOL SELECTION
//
// PURPOSE: Kill symbols that don't pay in your execution physics.
// Symbols are automatically disabled when metrics degrade.
//
// METRICS:
// - Sharpe ratio (30-day rolling)
// - Average edge vs latency cost
// - Reject rate
// - Fill rate
//
// PRUNING:
// - Low Sharpe → disable
// - Edge < latency cost → disable
// - High reject rate → disable
//
// RECOVERY:
// - Disabled symbols are re-tested periodically
// - Re-enabled only when metrics recover
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

namespace Chimera {
namespace Symbol {

// ─────────────────────────────────────────────────────────────────────────────
// Symbol Scorecard
// ─────────────────────────────────────────────────────────────────────────────
struct SymbolScorecard {
    char symbol[16] = {0};
    
    // Performance metrics
    double sharpe_30d = 0.0;
    double avg_edge_bps = 0.0;
    double win_rate = 0.0;
    
    // Execution metrics
    double latency_cost_bps = 0.0;
    double reject_rate = 0.0;
    double fill_rate = 0.0;
    
    // Trade counts
    uint32_t trades_30d = 0;
    uint32_t trades_today = 0;
    
    // Status
    bool enabled = true;
    bool in_recovery = false;
    uint64_t disabled_at_ns = 0;
    const char* disable_reason = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// Pruning Thresholds
// ─────────────────────────────────────────────────────────────────────────────
struct PruningThresholds {
    double min_sharpe = 0.5;
    double min_edge_to_cost_ratio = 1.2;  // Edge must be 1.2x latency cost
    double max_reject_rate = 0.25;
    double min_fill_rate = 0.4;
    uint32_t min_trades_for_decision = 20;
    
    uint64_t recovery_test_interval_ns = 3600'000'000'000ULL;  // 1 hour
};

// ─────────────────────────────────────────────────────────────────────────────
// Should Symbol Be Pruned?
// ─────────────────────────────────────────────────────────────────────────────
struct PruneDecision {
    bool should_prune = false;
    const char* reason = nullptr;
};

inline PruneDecision shouldPrune(
    const SymbolScorecard& s,
    const PruningThresholds& t = PruningThresholds{}
) {
    PruneDecision dec;
    
    // Need enough trades to decide
    if (s.trades_30d < t.min_trades_for_decision) {
        return dec;  // Not enough data
    }
    
    // Sharpe check
    if (s.sharpe_30d < t.min_sharpe) {
        dec.should_prune = true;
        dec.reason = "LOW_SHARPE";
        return dec;
    }
    
    // Edge vs cost check
    if (s.latency_cost_bps > 0 && 
        s.avg_edge_bps < s.latency_cost_bps * t.min_edge_to_cost_ratio) {
        dec.should_prune = true;
        dec.reason = "EDGE_BELOW_COST";
        return dec;
    }
    
    // Reject rate check
    if (s.reject_rate > t.max_reject_rate) {
        dec.should_prune = true;
        dec.reason = "HIGH_REJECTS";
        return dec;
    }
    
    // Fill rate check
    if (s.fill_rate < t.min_fill_rate && s.trades_30d > 50) {
        dec.should_prune = true;
        dec.reason = "LOW_FILL_RATE";
        return dec;
    }
    
    return dec;
}

// ─────────────────────────────────────────────────────────────────────────────
// Should Symbol Recover?
// ─────────────────────────────────────────────────────────────────────────────
inline bool shouldRecover(
    const SymbolScorecard& s,
    uint64_t now_ns,
    const PruningThresholds& t = PruningThresholds{}
) {
    if (!s.in_recovery) return false;
    
    // Wait for recovery interval
    if (now_ns - s.disabled_at_ns < t.recovery_test_interval_ns) {
        return false;
    }
    
    // Check if metrics have recovered
    auto dec = shouldPrune(s, t);
    return !dec.should_prune;
}

// ─────────────────────────────────────────────────────────────────────────────
// Symbol Manager
// ─────────────────────────────────────────────────────────────────────────────
class SymbolManager {
public:
    static constexpr size_t MAX_SYMBOLS = 20;
    
    void addSymbol(const char* symbol) {
        if (count_ >= MAX_SYMBOLS) return;
        SymbolScorecard s;
        strncpy(s.symbol, symbol, 15);
        cards_[count_++] = s;
    }
    
    SymbolScorecard* get(const char* symbol) {
        for (size_t i = 0; i < count_; i++) {
            if (strcmp(cards_[i].symbol, symbol) == 0) {
                return &cards_[i];
            }
        }
        return nullptr;
    }
    
    void updateMetrics(const char* symbol, double sharpe, double edge,
                       double latency_cost, double reject_rate, double fill_rate) {
        auto* s = get(symbol);
        if (!s) return;
        
        s->sharpe_30d = sharpe;
        s->avg_edge_bps = edge;
        s->latency_cost_bps = latency_cost;
        s->reject_rate = reject_rate;
        s->fill_rate = fill_rate;
    }
    
    void recordTrade(const char* symbol) {
        auto* s = get(symbol);
        if (!s) return;
        s->trades_30d++;
        s->trades_today++;
    }
    
    void evaluatePruning(uint64_t now_ns) {
        for (size_t i = 0; i < count_; i++) {
            auto& s = cards_[i];
            
            if (s.enabled) {
                auto dec = shouldPrune(s, thresh_);
                if (dec.should_prune) {
                    s.enabled = false;
                    s.in_recovery = true;
                    s.disabled_at_ns = now_ns;
                    s.disable_reason = dec.reason;
                }
            } else if (s.in_recovery) {
                if (shouldRecover(s, now_ns, thresh_)) {
                    s.enabled = true;
                    s.in_recovery = false;
                    s.disable_reason = nullptr;
                }
            }
        }
    }
    
    bool isEnabled(const char* symbol) const {
        for (size_t i = 0; i < count_; i++) {
            if (strcmp(cards_[i].symbol, symbol) == 0) {
                return cards_[i].enabled;
            }
        }
        return true;  // Unknown symbols default to enabled
    }
    
    std::vector<const char*> enabledSymbols() const {
        std::vector<const char*> result;
        for (size_t i = 0; i < count_; i++) {
            if (cards_[i].enabled) {
                result.push_back(cards_[i].symbol);
            }
        }
        return result;
    }
    
    void resetDaily() {
        for (size_t i = 0; i < count_; i++) {
            cards_[i].trades_today = 0;
        }
    }
    
private:
    SymbolScorecard cards_[MAX_SYMBOLS];
    size_t count_ = 0;
    PruningThresholds thresh_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Global Symbol Manager
// ─────────────────────────────────────────────────────────────────────────────
inline SymbolManager& getSymbolManager() {
    static SymbolManager mgr;
    return mgr;
}

} // namespace Symbol
} // namespace Chimera
