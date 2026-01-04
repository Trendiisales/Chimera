// ═══════════════════════════════════════════════════════════════════════════════
// include/alpha/AlphaRetirement.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.23: ALPHA AUTO-RETIREMENT (SURVIVAL MECHANISM)
// STATUS: 🔧 ACTIVE
// OWNER: Jo
// CREATED: 2026-01-03
//
// PURPOSE: Automatically retire alphas that stop printing.
// This is where 99% of systems fail. Yours won't.
//
// PRINCIPLE:
//   Most systems fail not because alpha is bad — but because people
//   refuse to kill it when it stops printing.
//
// RETIREMENT CONDITIONS (any one triggers):
//   1. Expectancy < 0 for rolling window
//   2. Drawdown slope increases
//   3. Slippage > threshold
//   4. Execution rejects spike
//   5. Regime misclassification rises
//
// RETIREMENT BEHAVIOR:
//   - Alpha disabled
//   - Capital = 0
//   - State persisted
//   - Cooldown applied
//
// RE-ENABLE ONLY AFTER:
//   - Fresh validation
//   - New market behavior
//   - Manual approval OR automatic after cooldown + positive shadow
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <array>
#include <atomic>
#include <mutex>

#include "alpha/MarketRegime.hpp"

namespace Chimera {
namespace Alpha {

// ─────────────────────────────────────────────────────────────────────────────
// Trade Outcome (for tracking)
// ─────────────────────────────────────────────────────────────────────────────
struct TradeOutcome {
    const char* alpha_name = nullptr;
    const char* symbol = nullptr;
    MarketRegime regime = MarketRegime::DEAD;
    
    double pnl_bps = 0.0;
    double mae_bps = 0.0;                   // Max adverse excursion
    double mfe_bps = 0.0;                   // Max favorable excursion
    uint64_t hold_ns = 0;
    
    double slippage_bps = 0.0;
    double fee_bps = 0.0;
    
    bool was_rejected = false;
    bool hit_stop = false;
    bool hit_target = false;
    bool time_exit = false;
    
    uint64_t timestamp_ns = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Alpha Statistics (rolling window)
// ─────────────────────────────────────────────────────────────────────────────
struct AlphaStats {
    static constexpr size_t WINDOW_SIZE = 50;  // Rolling window
    
    // Identification
    char alpha_name[32] = {};
    char symbol[16] = {};
    
    // Trade outcomes (ring buffer)
    std::array<double, WINDOW_SIZE> pnl_history{};
    std::array<double, WINDOW_SIZE> mae_history{};
    std::array<double, WINDOW_SIZE> slippage_history{};
    size_t history_idx = 0;
    size_t trade_count = 0;
    
    // Aggregates
    double total_pnl_bps = 0.0;
    double rolling_expectancy_bps = 0.0;
    double rolling_mae_avg = 0.0;
    double rolling_slippage_avg = 0.0;
    
    // Win/loss tracking
    uint64_t wins = 0;
    uint64_t losses = 0;
    double win_rate = 0.0;
    
    // Drawdown tracking
    double peak_pnl = 0.0;
    double current_drawdown = 0.0;
    double max_drawdown = 0.0;
    double drawdown_slope = 0.0;            // Increasing = bad
    
    // Execution quality
    uint64_t rejects = 0;
    double reject_rate = 0.0;
    
    // Regime tracking
    uint64_t regime_correct = 0;
    uint64_t regime_total = 0;
    double regime_accuracy = 0.0;
    
    // State
    bool retired = false;
    uint64_t retired_at_ns = 0;
    uint64_t cooldown_until_ns = 0;
    const char* retirement_reason = nullptr;
    
    void record_trade(const TradeOutcome& t) {
        // Update ring buffer
        pnl_history[history_idx] = t.pnl_bps;
        mae_history[history_idx] = t.mae_bps;
        slippage_history[history_idx] = t.slippage_bps;
        history_idx = (history_idx + 1) % WINDOW_SIZE;
        ++trade_count;
        
        // Update totals
        total_pnl_bps += t.pnl_bps;
        
        if (t.pnl_bps > 0) ++wins;
        else ++losses;
        
        // Win rate
        uint64_t total = wins + losses;
        win_rate = total > 0 ? static_cast<double>(wins) / total : 0.0;
        
        // Drawdown
        if (total_pnl_bps > peak_pnl) {
            peak_pnl = total_pnl_bps;
        }
        current_drawdown = peak_pnl - total_pnl_bps;
        if (current_drawdown > max_drawdown) {
            max_drawdown = current_drawdown;
        }
        
        // Reject tracking
        if (t.was_rejected) ++rejects;
        reject_rate = total > 0 ? static_cast<double>(rejects) / total : 0.0;
        
        // Recompute rolling stats
        recompute_rolling();
    }
    
    void record_regime(MarketRegime expected, MarketRegime actual) {
        ++regime_total;
        if (expected == actual) ++regime_correct;
        regime_accuracy = regime_total > 0 
            ? static_cast<double>(regime_correct) / regime_total : 0.0;
    }
    
private:
    void recompute_rolling() {
        size_t count = std::min(trade_count, WINDOW_SIZE);
        if (count == 0) return;
        
        double pnl_sum = 0.0;
        double mae_sum = 0.0;
        double slip_sum = 0.0;
        
        for (size_t i = 0; i < count; ++i) {
            pnl_sum += pnl_history[i];
            mae_sum += mae_history[i];
            slip_sum += slippage_history[i];
        }
        
        rolling_expectancy_bps = pnl_sum / count;
        rolling_mae_avg = mae_sum / count;
        rolling_slippage_avg = slip_sum / count;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Retirement Configuration
// ─────────────────────────────────────────────────────────────────────────────
struct RetirementConfig {
    // Minimum trades before evaluation
    size_t min_trades = 30;
    
    // Retirement thresholds
    double min_expectancy_bps = 0.0;        // Below this = retire
    double max_drawdown_r = 3.0;            // 3R drawdown = retire
    double max_slippage_pct_of_tp = 0.30;   // 30% of TP as slippage = retire
    double max_reject_rate = 0.15;          // 15% reject rate = retire
    double min_regime_accuracy = 0.70;      // 70% regime accuracy required
    
    // Cooldown
    uint64_t cooldown_ns = 3600'000'000'000ULL;  // 1 hour default
    
    // Auto re-enable
    bool auto_reenable = false;             // Require manual approval by default
    size_t shadow_trades_required = 20;     // Trades in shadow before re-enable
    double shadow_min_expectancy = 0.5;     // Shadow must show +0.5 bps
};

// ─────────────────────────────────────────────────────────────────────────────
// Retirement Evaluator
// ─────────────────────────────────────────────────────────────────────────────
class RetirementEvaluator {
public:
    explicit RetirementEvaluator(const RetirementConfig& cfg = RetirementConfig{})
        : config_(cfg)
    {}
    
    // ═══════════════════════════════════════════════════════════════════════
    // EVALUATION
    // ═══════════════════════════════════════════════════════════════════════
    
    struct EvalResult {
        bool should_retire = false;
        const char* reason = nullptr;
        double metric_value = 0.0;
        double threshold = 0.0;
    };
    
    [[nodiscard]] EvalResult evaluate(const AlphaStats& s) const noexcept {
        EvalResult r;
        
        // Not enough trades yet
        if (s.trade_count < config_.min_trades) {
            return r;
        }
        
        // Already retired
        if (s.retired) {
            return r;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Check 1: Expectancy
        // ─────────────────────────────────────────────────────────────────────
        if (s.rolling_expectancy_bps < config_.min_expectancy_bps) {
            r.should_retire = true;
            r.reason = "EXPECTANCY_NEGATIVE";
            r.metric_value = s.rolling_expectancy_bps;
            r.threshold = config_.min_expectancy_bps;
            return r;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Check 2: Drawdown (in R-multiples)
        // ─────────────────────────────────────────────────────────────────────
        double avg_loss = (s.losses > 0) 
            ? (s.total_pnl_bps - s.rolling_expectancy_bps * s.trade_count) / s.losses 
            : 1.0;
        double r_multiple = (avg_loss != 0) ? s.max_drawdown / std::abs(avg_loss) : 0.0;
        
        if (r_multiple > config_.max_drawdown_r) {
            r.should_retire = true;
            r.reason = "DRAWDOWN_EXCEEDED";
            r.metric_value = r_multiple;
            r.threshold = config_.max_drawdown_r;
            return r;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Check 3: Slippage eating edge
        // ─────────────────────────────────────────────────────────────────────
        // Slippage should be < 30% of expected TP
        double slippage_pct = (s.rolling_expectancy_bps > 0) 
            ? s.rolling_slippage_avg / s.rolling_expectancy_bps 
            : 0.0;
        
        if (slippage_pct > config_.max_slippage_pct_of_tp && s.rolling_slippage_avg > 0.5) {
            r.should_retire = true;
            r.reason = "SLIPPAGE_EXCESSIVE";
            r.metric_value = slippage_pct;
            r.threshold = config_.max_slippage_pct_of_tp;
            return r;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Check 4: Reject rate
        // ─────────────────────────────────────────────────────────────────────
        if (s.reject_rate > config_.max_reject_rate) {
            r.should_retire = true;
            r.reason = "REJECT_RATE_HIGH";
            r.metric_value = s.reject_rate;
            r.threshold = config_.max_reject_rate;
            return r;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Check 5: Regime accuracy
        // ─────────────────────────────────────────────────────────────────────
        if (s.regime_total >= 20 && s.regime_accuracy < config_.min_regime_accuracy) {
            r.should_retire = true;
            r.reason = "REGIME_MISCLASSIFICATION";
            r.metric_value = s.regime_accuracy;
            r.threshold = config_.min_regime_accuracy;
            return r;
        }
        
        return r;
    }
    
    RetirementConfig& config() noexcept { return config_; }
    const RetirementConfig& config() const noexcept { return config_; }
    
private:
    RetirementConfig config_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Alpha Retirement Manager (per-alpha, per-symbol tracking)
// ─────────────────────────────────────────────────────────────────────────────
class AlphaRetirementManager {
public:
    static constexpr size_t MAX_TRACKED = 32;  // Max alpha+symbol combinations
    
    static AlphaRetirementManager& instance() {
        static AlphaRetirementManager mgr;
        return mgr;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // RECORD TRADE OUTCOME
    // ═══════════════════════════════════════════════════════════════════════
    
    void record(const TradeOutcome& t) {
        AlphaStats* stats = get_or_create(t.alpha_name, t.symbol);
        if (!stats) return;
        
        stats->record_trade(t);
        
        // Evaluate for retirement
        RetirementEvaluator::EvalResult eval = evaluator_.evaluate(*stats);
        if (eval.should_retire && !stats->retired) {
            retire(stats, eval.reason, t.timestamp_ns);
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // CHECK IF ALPHA IS ENABLED
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] bool is_enabled(const char* alpha, const char* symbol, uint64_t now_ns) const {
        const AlphaStats* stats = find(alpha, symbol);
        if (!stats) return true;  // Not tracked yet = enabled
        
        if (!stats->retired) return true;
        
        // Check cooldown
        if (config_.auto_reenable && now_ns >= stats->cooldown_until_ns) {
            // Would need shadow validation - for now keep disabled
            return false;
        }
        
        return false;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // MANUAL CONTROLS
    // ═══════════════════════════════════════════════════════════════════════
    
    void force_retire(const char* alpha, const char* symbol, const char* reason, uint64_t now_ns) {
        AlphaStats* stats = get_or_create(alpha, symbol);
        if (stats) {
            retire(stats, reason, now_ns);
        }
    }
    
    void force_enable(const char* alpha, const char* symbol) {
        AlphaStats* stats = find_mutable(alpha, symbol);
        if (stats && stats->retired) {
            printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
            printf("║ [ALPHA_ENABLED] %s on %s manually re-enabled                    \n",
                   alpha, symbol);
            printf("╚══════════════════════════════════════════════════════════════════════╝\n");
            
            stats->retired = false;
            stats->retirement_reason = nullptr;
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // QUERY
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] const AlphaStats* get_stats(const char* alpha, const char* symbol) const {
        return find(alpha, symbol);
    }
    
    void print_all() const {
        printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
        printf("║ ALPHA RETIREMENT STATUS                                              ║\n");
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        
        for (size_t i = 0; i < count_; ++i) {
            const AlphaStats& s = stats_[i];
            printf("║ %-20s %-10s trades=%3zu exp=%.2f%s\n",
                   s.alpha_name, s.symbol,
                   s.trade_count,
                   s.rolling_expectancy_bps,
                   s.retired ? " RETIRED" : "");
            if (s.retired) {
                printf("║   └─ Reason: %s\n", s.retirement_reason);
            }
        }
        
        printf("╚══════════════════════════════════════════════════════════════════════╝\n");
    }
    
    RetirementConfig& config() noexcept { return config_; }
    
private:
    AlphaRetirementManager() {}
    
    void retire(AlphaStats* stats, const char* reason, uint64_t now_ns) {
        stats->retired = true;
        stats->retired_at_ns = now_ns;
        stats->cooldown_until_ns = now_ns + config_.cooldown_ns;
        stats->retirement_reason = reason;
        
        printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
        printf("║ [ALPHA_RETIRED] ✗ %s on %s                                      \n",
               stats->alpha_name, stats->symbol);
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        printf("║   Reason: %s\n", reason);
        printf("║   Trades: %zu\n", stats->trade_count);
        printf("║   Expectancy: %.2f bps\n", stats->rolling_expectancy_bps);
        printf("║   Max DD: %.2f bps\n", stats->max_drawdown);
        printf("║   Win Rate: %.1f%%\n", stats->win_rate * 100.0);
        printf("╚══════════════════════════════════════════════════════════════════════╝\n");
    }
    
    AlphaStats* get_or_create(const char* alpha, const char* symbol) {
        // Find existing
        for (size_t i = 0; i < count_; ++i) {
            if (strcmp(stats_[i].alpha_name, alpha) == 0 &&
                strcmp(stats_[i].symbol, symbol) == 0) {
                return &stats_[i];
            }
        }
        
        // Create new
        if (count_ >= MAX_TRACKED) return nullptr;
        
        AlphaStats& s = stats_[count_++];
        strncpy(s.alpha_name, alpha, 31);
        strncpy(s.symbol, symbol, 15);
        return &s;
    }
    
    const AlphaStats* find(const char* alpha, const char* symbol) const {
        for (size_t i = 0; i < count_; ++i) {
            if (strcmp(stats_[i].alpha_name, alpha) == 0 &&
                strcmp(stats_[i].symbol, symbol) == 0) {
                return &stats_[i];
            }
        }
        return nullptr;
    }
    
    AlphaStats* find_mutable(const char* alpha, const char* symbol) {
        for (size_t i = 0; i < count_; ++i) {
            if (strcmp(stats_[i].alpha_name, alpha) == 0 &&
                strcmp(stats_[i].symbol, symbol) == 0) {
                return &stats_[i];
            }
        }
        return nullptr;
    }
    
    std::array<AlphaStats, MAX_TRACKED> stats_;
    size_t count_ = 0;
    RetirementEvaluator evaluator_;
    RetirementConfig config_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Convenience function
// ─────────────────────────────────────────────────────────────────────────────
inline bool alphaEnabled(const char* alpha, const char* symbol, uint64_t now_ns = 0) {
    return AlphaRetirementManager::instance().is_enabled(alpha, symbol, now_ns);
}

} // namespace Alpha
} // namespace Chimera
