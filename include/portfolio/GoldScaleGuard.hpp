// =============================================================================
// GoldScaleGuard.hpp - DISCIPLINE-BASED GOLD SCALING
// =============================================================================
// Gold scales on DISCIPLINE, not profits.
//
// SCALE LEVELS:
//   MICRO (0.10%) - Default, must prove discipline
//   LEVEL_1 (0.20%) - After 30 trades with metrics passing
//   LEVEL_2 (0.30%) - After 60 trades with continued discipline
//
// METRICS REQUIRED TO SCALE:
//   - 90%+ trades from ACTIVE campaign
//   - 0 runners turning into losers
//   - 70%+ losses are soft (≤ -0.6R)
//   - 60%+ winners take partials
//
// If discipline degrades, scaling FREEZES automatically.
// =============================================================================
#pragma once

#include <vector>
#include <cstdint>
#include <cstdio>

namespace portfolio {

// =============================================================================
// SCALE LEVELS
// =============================================================================
enum class GoldScaleLevel : uint8_t {
    DISABLED = 0,
    MICRO    = 1,   // 0.10% - proving ground
    LEVEL_1  = 2,   // 0.20% - allowed contributor
    LEVEL_2  = 3    // 0.30% - portfolio additive
};

inline const char* goldScaleLevelStr(GoldScaleLevel level) {
    switch (level) {
        case GoldScaleLevel::DISABLED: return "DISABLED";
        case GoldScaleLevel::MICRO:    return "MICRO";
        case GoldScaleLevel::LEVEL_1:  return "LEVEL_1";
        case GoldScaleLevel::LEVEL_2:  return "LEVEL_2";
        default:                       return "UNKNOWN";
    }
}

// =============================================================================
// TRADE STATS (Input for evaluation)
// =============================================================================
struct GoldTradeStats {
    double pnl_r            = 0.0;
    bool   from_campaign    = false;  // Was campaign ACTIVE at entry?
    bool   took_partial     = false;  // Did we take partial at target?
    bool   runner_failed    = false;  // Did winner turn into loser?
};

// =============================================================================
// GOLD SCALE GUARD
// =============================================================================
class GoldScaleGuard {
public:
    GoldScaleGuard() = default;

    // -------------------------------------------------------------------------
    // RECORD TRADE
    // -------------------------------------------------------------------------
    void recordTrade(const GoldTradeStats& trade) {
        trades_.push_back(trade);
        
        // Keep rolling window of 60 trades
        if (trades_.size() > MAX_TRADES) {
            trades_.erase(trades_.begin());
        }
        
        evaluate();
        
        printf("[GOLD-SCALE] Trade recorded: pnl=%.2fR campaign=%s partial=%s runner_fail=%s\n",
               trade.pnl_r,
               trade.from_campaign ? "YES" : "NO",
               trade.took_partial ? "YES" : "NO",
               trade.runner_failed ? "YES" : "NO");
        printf("[GOLD-SCALE] Status: level=%s allowed=%s trades=%zu\n",
               goldScaleLevelStr(level_), scale_allowed_ ? "YES" : "NO", trades_.size());
    }

    // -------------------------------------------------------------------------
    // QUERIES
    // -------------------------------------------------------------------------
    GoldScaleLevel getLevel() const { return level_; }
    bool scaleAllowed() const { return scale_allowed_; }
    size_t getTradeCount() const { return trades_.size(); }
    
    double getRiskPct() const {
        switch (level_) {
            case GoldScaleLevel::MICRO:   return 0.001;  // 0.10%
            case GoldScaleLevel::LEVEL_1: return 0.002;  // 0.20%
            case GoldScaleLevel::LEVEL_2: return 0.003;  // 0.30%
            default:                      return 0.0;
        }
    }
    
    const char* getStatus() const {
        return scale_allowed_ ? "SCALE_ALLOWED" : "SCALE_FROZEN";
    }

    // -------------------------------------------------------------------------
    // RESET
    // -------------------------------------------------------------------------
    void reset() {
        trades_.clear();
        level_ = GoldScaleLevel::MICRO;
        scale_allowed_ = false;
        printf("[GOLD-SCALE] Reset to MICRO\n");
    }
    
    // -------------------------------------------------------------------------
    // STATUS PRINT
    // -------------------------------------------------------------------------
    void printStatus() const {
        printf("\n[GOLD-SCALE] ═══════════════════════════════════════\n");
        printf("[GOLD-SCALE] Level: %s\n", goldScaleLevelStr(level_));
        printf("[GOLD-SCALE] Risk: %.2f%%\n", getRiskPct() * 100.0);
        printf("[GOLD-SCALE] Status: %s\n", getStatus());
        printf("[GOLD-SCALE] Trades: %zu / %zu required\n", trades_.size(), MIN_TRADES_FOR_SCALE);
        
        if (trades_.size() >= 10) {
            // Show metrics
            int from_campaign = 0, partials = 0, runner_fail = 0, soft_losses = 0;
            for (const auto& t : trades_) {
                if (t.from_campaign) from_campaign++;
                if (t.took_partial) partials++;
                if (t.runner_failed) runner_fail++;
                if (t.pnl_r >= -0.6) soft_losses++;
            }
            double n = static_cast<double>(trades_.size());
            
            printf("[GOLD-SCALE] Campaign discipline: %.0f%% (need 90%%)\n", (from_campaign / n) * 100.0);
            printf("[GOLD-SCALE] Partial rate: %.0f%% (need 60%%)\n", (partials / n) * 100.0);
            printf("[GOLD-SCALE] Runner failures: %d (need 0)\n", runner_fail);
            printf("[GOLD-SCALE] Soft losses: %.0f%% (need 70%%)\n", (soft_losses / n) * 100.0);
        }
        printf("[GOLD-SCALE] ═══════════════════════════════════════\n\n");
    }

private:
    static constexpr size_t MAX_TRADES = 60;
    static constexpr size_t MIN_TRADES_FOR_SCALE = 30;

    std::vector<GoldTradeStats> trades_;
    GoldScaleLevel level_ = GoldScaleLevel::MICRO;
    bool scale_allowed_ = false;

    // -------------------------------------------------------------------------
    // EVALUATE DISCIPLINE
    // -------------------------------------------------------------------------
    void evaluate() {
        // Need minimum trades before evaluating
        if (trades_.size() < MIN_TRADES_FOR_SCALE) {
            scale_allowed_ = false;
            return;
        }

        // Count metrics
        int from_campaign = 0;
        int partials = 0;
        int runner_fail = 0;
        int soft_losses = 0;

        for (const auto& t : trades_) {
            if (t.from_campaign) from_campaign++;
            if (t.took_partial) partials++;
            if (t.runner_failed) runner_fail++;
            if (t.pnl_r >= -0.6) soft_losses++;  // Soft loss = ≤ 0.6R
        }

        double n = static_cast<double>(trades_.size());

        // =====================================================================
        // DISCIPLINE METRICS (ALL MUST PASS)
        // =====================================================================
        bool discipline_ok = 
            (from_campaign / n) >= 0.90 &&   // 90%+ from ACTIVE campaign
            runner_fail == 0;                 // 0 runners turning losers

        bool loss_shape_ok = 
            (soft_losses / n) >= 0.70;        // 70%+ losses are soft

        bool exit_quality_ok = 
            (partials / n) >= 0.60;           // 60%+ take partials

        scale_allowed_ = discipline_ok && loss_shape_ok && exit_quality_ok;

        // =====================================================================
        // LEVEL PROGRESSION (ONLY IF ALLOWED)
        // =====================================================================
        if (!scale_allowed_) {
            // Don't demote, just freeze
            return;
        }

        if (level_ == GoldScaleLevel::MICRO && trades_.size() >= 30) {
            level_ = GoldScaleLevel::LEVEL_1;
            printf("[GOLD-SCALE] *** PROMOTED TO LEVEL_1 (0.20%%) ***\n");
        } else if (level_ == GoldScaleLevel::LEVEL_1 && trades_.size() >= 60) {
            level_ = GoldScaleLevel::LEVEL_2;
            printf("[GOLD-SCALE] *** PROMOTED TO LEVEL_2 (0.30%%) ***\n");
        }
    }
};

} // namespace portfolio
