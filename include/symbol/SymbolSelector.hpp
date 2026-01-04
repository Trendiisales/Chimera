// ═══════════════════════════════════════════════════════════════════════════════
// include/symbol/SymbolSelector.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.12: CROSS-SYMBOL OPPORTUNITY SUBSTITUTION
//
// PURPOSE: Capital flows to where opportunity is, signals don't.
// Most systems fixate on one symbol and force trades when it's dead.
// Institutions ask: "Where is opportunity today?"
//
// IMPLEMENTATION:
// - Live scorecard per symbol
// - Expectancy-based ranking
// - Automatic rotation to best opportunities
// - Only trade top N symbols by score
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include "SymbolScorecard.hpp"
#include "../alpha/MarketRegime.hpp"
#include "../execution/SessionWeights.hpp"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <chrono>

namespace Chimera {
namespace Symbol {

// ─────────────────────────────────────────────────────────────────────────────
// Extended Symbol Score - For ranking
// ─────────────────────────────────────────────────────────────────────────────
struct SymbolScore {
    char symbol[16] = {0};
    double expectancy = 0.0;           // Live expectancy estimate
    double fill_quality = 0.0;         // Fill rate quality
    double latency_cost = 0.0;         // Latency penalty
    double volatility = 0.0;           // Current volatility
    double conviction_rate = 0.0;      // % of signals with high conviction
    double execution_cost = 0.0;       // Spread + fees + slippage
    double session_weight = 1.0;       // Time-of-day adjustment
    Alpha::MarketRegime regime = Alpha::MarketRegime::DEAD;
    bool enabled = true;
    double final_score = 0.0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Score Calculation - Execution-aware expectancy
// ─────────────────────────────────────────────────────────────────────────────
struct ScoreConfig {
    double latency_weight = 0.4;       // Penalty for high latency
    double fill_weight = 0.3;          // Bonus for good fills
    double volatility_weight = 0.2;    // Bonus for volatility (opportunity)
    double conviction_weight = 0.1;    // Bonus for signal quality
    double min_score_to_trade = 0.0;   // Score threshold
};

inline double computeSymbolScore(const SymbolScore& s, 
                                  const ScoreConfig& cfg = ScoreConfig{}) {
    if (!s.enabled) return -999.0;
    if (s.regime == Alpha::MarketRegime::DEAD) return -99.0;
    
    double score = s.expectancy;
    score -= s.latency_cost * cfg.latency_weight;
    score += s.fill_quality * cfg.fill_weight;
    score += s.volatility * cfg.volatility_weight;
    score += s.conviction_rate * cfg.conviction_weight;
    score -= s.execution_cost;
    score *= s.session_weight;
    
    return score;
}

// ─────────────────────────────────────────────────────────────────────────────
// Symbol Selection Result
// ─────────────────────────────────────────────────────────────────────────────
struct SelectionResult {
    std::vector<const char*> selected;
    int total_evaluated = 0;
    int passed_threshold = 0;
    double best_score = 0.0;
    const char* best_symbol = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// Select Top N Symbols by Score
// ─────────────────────────────────────────────────────────────────────────────
inline SelectionResult selectSymbols(
    SymbolScore* scores,
    size_t count,
    size_t max_symbols = 2,
    const ScoreConfig& cfg = ScoreConfig{}
) {
    SelectionResult result;
    result.total_evaluated = static_cast<int>(count);
    
    // Calculate scores
    for (size_t i = 0; i < count; i++) {
        scores[i].final_score = computeSymbolScore(scores[i], cfg);
    }
    
    // Sort by score descending (simple bubble sort for small N)
    for (size_t i = 0; i < count - 1; i++) {
        for (size_t j = i + 1; j < count; j++) {
            if (scores[j].final_score > scores[i].final_score) {
                std::swap(scores[i], scores[j]);
            }
        }
    }
    
    // Select top N that meet threshold
    for (size_t i = 0; i < count && result.selected.size() < max_symbols; i++) {
        if (scores[i].final_score > cfg.min_score_to_trade) {
            result.selected.push_back(scores[i].symbol);
            result.passed_threshold++;
            
            if (result.best_symbol == nullptr) {
                result.best_symbol = scores[i].symbol;
                result.best_score = scores[i].final_score;
            }
        }
    }
    
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Live Symbol Selector - Manages active symbol rotation
// ─────────────────────────────────────────────────────────────────────────────
class LiveSymbolSelector {
public:
    static constexpr size_t MAX_SYMBOLS = 16;
    static constexpr size_t MAX_ACTIVE = 3;      // Max concurrent symbols
    
    // v4.9.12 HARDENING: Minimum hold duration to prevent churn
    // Institutional rule: don't rotate unless symbol is truly dead
    static constexpr uint64_t MIN_HOLD_DURATION_NS = 5ULL * 60ULL * 1000000000ULL;  // 5 minutes
    static constexpr double EMERGENCY_DEMOTION_THRESHOLD = -5.0;  // Force rotation if score drops this low
    
    // Add symbol to tracking
    bool addSymbol(const char* symbol) {
        if (count_ >= MAX_SYMBOLS) return false;
        
        strncpy(scores_[count_].symbol, symbol, 15);
        scores_[count_].enabled = true;
        count_++;
        return true;
    }
    
    // Update symbol metrics
    void updateMetrics(const char* symbol, 
                       double expectancy,
                       double fill_quality,
                       double latency_cost,
                       double volatility,
                       double conviction_rate,
                       double execution_cost) {
        SymbolScore* s = getScore(symbol);
        if (!s) return;
        
        s->expectancy = expectancy;
        s->fill_quality = fill_quality;
        s->latency_cost = latency_cost;
        s->volatility = volatility;
        s->conviction_rate = conviction_rate;
        s->execution_cost = execution_cost;
    }
    
    // Update regime for symbol
    void updateRegime(const char* symbol, Alpha::MarketRegime regime) {
        SymbolScore* s = getScore(symbol);
        if (s) s->regime = regime;
    }
    
    // Update session weight
    void updateSessionWeight(const char* symbol, double weight) {
        SymbolScore* s = getScore(symbol);
        if (s) s->session_weight = weight;
    }
    
    // Enable/disable symbol
    void setEnabled(const char* symbol, bool enabled) {
        SymbolScore* s = getScore(symbol);
        if (s) s->enabled = enabled;
    }
    
    // Re-evaluate and select active symbols
    SelectionResult evaluate() {
        return selectSymbols(scores_, count_, MAX_ACTIVE, config_);
    }
    
    // Check if symbol is currently selected
    bool isActive(const char* symbol) const {
        for (const auto* sym : active_) {
            if (sym && strcmp(sym, symbol) == 0) return true;
        }
        return false;
    }
    
    // Update active list
    // v4.9.12 HARDENING: Respects minimum hold duration unless emergency
    void updateActiveList() {
        uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        auto result = evaluate();
        
        // Check if current active symbols should be kept (hold duration)
        std::vector<const char*> new_active;
        
        for (size_t i = 0; i < active_.size(); i++) {
            const char* sym = active_[i];
            const SymbolScore* score = getScore(sym);
            
            // Emergency demotion: score dropped catastrophically
            if (score && score->final_score < EMERGENCY_DEMOTION_THRESHOLD) {
                continue;  // Drop this symbol immediately
            }
            
            // Hold duration check: keep if not enough time elapsed
            uint64_t hold_ns = now_ns - active_since_ns_[i];
            if (hold_ns < MIN_HOLD_DURATION_NS) {
                new_active.push_back(sym);
                continue;  // Keep this symbol, hold period not expired
            }
            
            // Check if still in top N
            bool still_top = false;
            for (const char* top_sym : result.selected) {
                if (strcmp(sym, top_sym) == 0) {
                    still_top = true;
                    break;
                }
            }
            
            if (still_top) {
                new_active.push_back(sym);
            }
        }
        
        // Add new symbols if we have room
        for (const char* sym : result.selected) {
            if (new_active.size() >= MAX_ACTIVE) break;
            
            bool already_active = false;
            for (const char* active_sym : new_active) {
                if (strcmp(sym, active_sym) == 0) {
                    already_active = true;
                    break;
                }
            }
            
            if (!already_active) {
                new_active.push_back(sym);
                // Track when this symbol became active
                if (new_active.size() <= MAX_ACTIVE) {
                    active_since_ns_[new_active.size() - 1] = now_ns;
                }
            }
        }
        
        // Update active list and timestamps
        active_.clear();
        for (size_t i = 0; i < new_active.size(); i++) {
            active_.push_back(new_active[i]);
        }
    }
    
    // Get score for symbol
    SymbolScore* getScore(const char* symbol) {
        for (size_t i = 0; i < count_; i++) {
            if (strcmp(scores_[i].symbol, symbol) == 0) {
                return &scores_[i];
            }
        }
        return nullptr;
    }
    
    const SymbolScore* getScore(const char* symbol) const {
        for (size_t i = 0; i < count_; i++) {
            if (strcmp(scores_[i].symbol, symbol) == 0) {
                return &scores_[i];
            }
        }
        return nullptr;
    }
    
    // Get all scores sorted by rank
    const SymbolScore* getRanked(size_t* out_count) const {
        *out_count = count_;
        return scores_;
    }
    
    // Get best symbol right now
    const char* getBestSymbol() const {
        double best_score = -999.0;
        const char* best = nullptr;
        
        for (size_t i = 0; i < count_; i++) {
            double score = computeSymbolScore(scores_[i], config_);
            if (score > best_score && scores_[i].enabled) {
                best_score = score;
                best = scores_[i].symbol;
            }
        }
        
        return best;
    }
    
    // Print current rankings
    void printRankings() const {
        printf("\n══════════════════════════════════════════════════════════════\n");
        printf("  SYMBOL RANKINGS (by opportunity score)\n");
        printf("══════════════════════════════════════════════════════════════\n");
        
        // Create temp array for sorting
        SymbolScore sorted[MAX_SYMBOLS];
        for (size_t i = 0; i < count_; i++) {
            sorted[i] = scores_[i];
            sorted[i].final_score = computeSymbolScore(sorted[i], config_);
        }
        
        // Sort
        for (size_t i = 0; i < count_ - 1; i++) {
            for (size_t j = i + 1; j < count_; j++) {
                if (sorted[j].final_score > sorted[i].final_score) {
                    std::swap(sorted[i], sorted[j]);
                }
            }
        }
        
        // Print
        for (size_t i = 0; i < count_; i++) {
            const auto& s = sorted[i];
            printf("  %2zu. %-10s: score=%+.3f exp=%.3f regime=%s %s\n",
                   i + 1,
                   s.symbol,
                   s.final_score,
                   s.expectancy,
                   Alpha::regimeStr(s.regime),
                   s.enabled ? "" : "[DISABLED]");
        }
        
        printf("══════════════════════════════════════════════════════════════\n\n");
    }
    
    ScoreConfig& config() { return config_; }
    const ScoreConfig& config() const { return config_; }
    size_t count() const { return count_; }
    
private:
    SymbolScore scores_[MAX_SYMBOLS];
    size_t count_ = 0;
    std::vector<const char*> active_;
    uint64_t active_since_ns_[MAX_ACTIVE] = {0};  // v4.9.12: Track when each symbol became active
    ScoreConfig config_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Global Symbol Selector
// ─────────────────────────────────────────────────────────────────────────────
inline LiveSymbolSelector& getSymbolSelector() {
    static LiveSymbolSelector selector;
    return selector;
}

// ─────────────────────────────────────────────────────────────────────────────
// Quick Check: Is symbol worth trading right now?
// ─────────────────────────────────────────────────────────────────────────────
inline bool isSymbolWorthTrading(const char* symbol) {
    auto& selector = getSymbolSelector();
    const SymbolScore* score = selector.getScore(symbol);
    
    if (!score) return false;
    if (!score->enabled) return false;
    if (score->regime == Alpha::MarketRegime::DEAD) return false;
    if (score->expectancy < 0.0) return false;
    
    // Check if in top N
    return selector.isActive(symbol) || 
           symbol == selector.getBestSymbol();
}

} // namespace Symbol
} // namespace Chimera
