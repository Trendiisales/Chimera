// =============================================================================
// GoldImpulseEngine_v1.hpp - VALIDATED GOLD IMPULSE ENGINE (ENGINE A)
// =============================================================================
// 🔒 THIS ENGINE IS LOCKED - DO NOT MODIFY WITHOUT EVIDENCE
//
// Ported from validated Python gold_impulse_engine_v1.py
// Replaces GoldEngine_v5_2 with Monte Carlo validated parameters
//
// KEY DIFFERENCES FROM v5.2:
//   - Partial at +1.0R (not 1.5R) - validated
//   - Partial 40% (not 60%) - validated  
//   - Trail arms at +1.5R (not 3R) - validated
//   - Stop moves to BE at partial (no buffer) - validated
//   - Campaign geometry trailing at 50% of range
//
// MONTE CARLO VALIDATED STATS:
//   - Expectancy: +0.62R per trade
//   - Win rate: ~85-90%
//   - Max DD: ~1.1R
//   - No fat tail losses
//
// EXPLICITLY FORBIDDEN:
//   - ❌ Pyramids (structurally incompatible with impulse trades)
//   - ❌ Parameter tweaks without backtesting
//   - ❌ Adding complexity
// =============================================================================
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <chrono>

#include "engines/GoldCampaignEngine_v2_1.hpp"

namespace gold {

// =============================================================================
// POSITION STATE
// =============================================================================
enum class ImpulsePositionState : uint8_t {
    FLAT    = 0,
    OPEN    = 1,
    PARTIAL = 2
};

inline const char* impulsePositionStateStr(ImpulsePositionState s) {
    switch (s) {
        case ImpulsePositionState::OPEN:    return "OPEN";
        case ImpulsePositionState::PARTIAL: return "PARTIAL";
        default:                            return "FLAT";
    }
}

// =============================================================================
// POSITION
// =============================================================================
struct ImpulsePosition {
    int     side         = 0;       // +1 long, -1 short
    double  entry        = 0.0;
    double  stop         = 0.0;
    double  trail        = 0.0;     // Structural trail price
    double  initial_risk = 0.0;
    double  size         = 0.0;
    double  remaining_size = 0.0;   // After partial
    uint64_t entry_ts    = 0;
    
    // Campaign geometry (frozen at entry)
    double observe_low   = 0.0;
    double observe_high  = 0.0;
};

// =============================================================================
// TRADE RECORD (same interface as v5.2 for compatibility)
// =============================================================================
struct ImpulseTradeRecord {
    char symbol[16]     = "XAUUSD";
    int8_t side         = 0;
    double entry_price  = 0.0;
    double exit_price   = 0.0;
    double size         = 0.0;
    double pnl_dollars  = 0.0;
    double pnl_r        = 0.0;
    char exit_reason[32] = {0};
    bool is_partial     = false;
    uint64_t entry_ts   = 0;
    uint64_t exit_ts    = 0;
};

// =============================================================================
// GOLD IMPULSE ENGINE v1 (VALIDATED - LOCKED)
// =============================================================================
class GoldImpulseEngine_v1 {
public:
    using OrderCallback = std::function<void(const char* symbol, bool is_buy, double qty)>;
    using TradeCallback = std::function<void(const ImpulseTradeRecord& trade)>;
    using ScaleStatsCallback = std::function<void(double pnl_r, bool from_campaign, bool took_partial, bool runner_failed)>;
    using PermissionCallback = std::function<bool(double* risk_pct, const char** reason)>;

    GoldImpulseEngine_v1() = default;

    // -------------------------------------------------------------------------
    // CONFIGURATION (same interface as v5.2)
    // -------------------------------------------------------------------------
    void setOrderCallback(OrderCallback cb) { order_callback_ = std::move(cb); }
    void setTradeCallback(TradeCallback cb) { trade_callback_ = std::move(cb); }
    void setScaleStatsCallback(ScaleStatsCallback cb) { scale_stats_callback_ = std::move(cb); }
    void setPermissionCallback(PermissionCallback cb) { permission_callback_ = std::move(cb); }
    void setEquity(double equity) { equity_ = equity; }
    void setRiskPercent(double pct) { risk_pct_ = pct; }

    // -------------------------------------------------------------------------
    // FEEDS (same interface as v5.2)
    // -------------------------------------------------------------------------
    void onH1Bar(double h, double l, double c) {
        campaign_.onH1Bar({h, l, c});
    }

    void onM5Bar(double o, double h, double l, double c) {
        campaign_.onM5Bar({o, h, l, c});
        last_price_ = c;
        last_high_ = h;
        last_low_ = l;
        bars_held_++;

        managePosition();
        tryEnter();
    }

    // -------------------------------------------------------------------------
    // STATE QUERIES (same interface as v5.2)
    // -------------------------------------------------------------------------
    bool hasPosition() const {
        return state_ != ImpulsePositionState::FLAT;
    }
    
    ImpulsePositionState getState() const { return state_; }
    const ImpulsePosition& getPosition() const { return pos_; }
    const CampaignContext& getCampaign() const { return campaign_.getCampaign(); }
    HTFRegime getRegime() const { return campaign_.getRegime(); }
    int tradesToday() const { return trades_today_; }
    
    void resetDaily() {
        trades_today_ = 0;
    }

    void reset() {
        state_ = ImpulsePositionState::FLAT;
        pos_ = ImpulsePosition{};
        campaign_.reset();
        trades_today_ = 0;
        bars_held_ = 0;
        max_r_reached_ = 0.0;
        trail_armed_ = false;
    }
    
    void forceExit(const char* reason = "FORCED") {
        if (state_ != ImpulsePositionState::FLAT) {
            exitAll(reason);
        }
    }

private:
    // -------------------------------------------------------------------------
    // STATE
    // -------------------------------------------------------------------------
    GoldCampaignEngine campaign_;
    ImpulsePositionState state_ = ImpulsePositionState::FLAT;
    ImpulsePosition pos_;
    
    double last_price_ = 0.0;
    double last_high_ = 0.0;
    double last_low_ = 0.0;
    
    double equity_ = 100000.0;
    double risk_pct_ = 0.001;  // 0.10% default
    int trades_today_ = 0;
    int bars_held_ = 0;
    
    // Tracking
    bool entry_from_campaign_ = false;
    bool took_partial_ = false;
    double max_r_reached_ = 0.0;
    bool trail_armed_ = false;

    OrderCallback order_callback_;
    TradeCallback trade_callback_;
    ScaleStatsCallback scale_stats_callback_;
    PermissionCallback permission_callback_;

    // =========================================================================
    // CONSTANTS (VALIDATED - DO NOT CHANGE)
    // =========================================================================
    static constexpr double PARTIAL_R         = 1.0;    // Take partial at 1R (validated)
    static constexpr double PARTIAL_PCT       = 0.40;   // Exit 40% at partial (validated)
    static constexpr double TRAIL_ARM_R       = 1.5;    // Trail arms at 1.5R (validated)
    static constexpr double TRAIL_FRAC        = 0.50;   // Trail at 50% of campaign range
    static constexpr int    MAX_TRADES_DAY    = 1;      // One trade per day max
    static constexpr int    MAX_HOLD_BARS     = 72;     // 360 min / 5 = 72 bars
    static constexpr double CONFIDENCE_MIN    = 0.40;   // v2.2: lowered from 0.45
    static constexpr double EXTENSION_CONF    = 0.60;   // High confidence gets extension
    static constexpr int    EXTENSION_BARS    = 2;      // +10 minutes for high confidence

    // -------------------------------------------------------------------------
    // ENTRY LOGIC
    // -------------------------------------------------------------------------
    void tryEnter() {
        if (state_ != ImpulsePositionState::FLAT) return;
        if (trades_today_ >= MAX_TRADES_DAY) return;

        const auto& ctx = campaign_.getCampaign();
        
        // HARD GATE: Campaign must be ACTIVE
        if (ctx.state != CampaignState::ACTIVE) return;
        
        // v2.2: Confidence threshold lowered to 0.40
        if (ctx.confidence < CONFIDENCE_MIN) return;

        // =====================================================================
        // PORTFOLIO PERMISSION GATE (NON-NEGOTIABLE)
        // =====================================================================
        double portfolio_risk = risk_pct_;
        const char* permission_reason = "DEFAULT";
        
        if (permission_callback_) {
            bool allowed = permission_callback_(&portfolio_risk, &permission_reason);
            if (!allowed) {
                printf("[IMPULSE-A] GOLD_PERMISSION=DENIED reason=%s\n", permission_reason);
                return;
            }
            printf("[IMPULSE-A] GOLD_PERMISSION=GRANTED risk=%.2f%% reason=%s\n", 
                   portfolio_risk * 100.0, permission_reason);
            risk_pct_ = portfolio_risk;
        }

        int side = (ctx.direction == HTFRegime::LONG) ? +1 : -1;

        // Entry distance check (within 50% of campaign range from key level)
        double range = campaignRange();
        double dist = std::abs(last_price_ - ctx.key_level);
        if (dist > 0.5 * range) {
            return;  // Too extended
        }

        // STRUCTURAL STOP: Beyond observe range
        double stop = (side > 0)
            ? ctx.observe_low - 2.0   // Buffer below observe low
            : ctx.observe_high + 2.0; // Buffer above observe high
            
        double stop_dist = std::abs(last_price_ - stop);
        if (stop_dist < 10.0 || stop_dist > 50.0) {
            printf("[IMPULSE-A] Entry rejected: stop_dist=%.2f out of range [10,50]\n", stop_dist);
            return;
        }

        // Calculate size from risk
        double risk_dollars = equity_ * risk_pct_;
        double size = risk_dollars / stop_dist;
        size = std::floor(size * 100.0) / 100.0;  // Round to 0.01
        size = std::max(0.01, std::min(size, 1.0));

        // Fill position
        pos_.side = side;
        pos_.entry = last_price_;
        pos_.stop = stop;
        pos_.trail = stop;  // Trail starts at stop
        pos_.initial_risk = stop_dist;
        pos_.size = size;
        pos_.remaining_size = size;
        pos_.entry_ts = nowNs();
        pos_.observe_low = ctx.observe_low;
        pos_.observe_high = ctx.observe_high;
        
        // Reset tracking
        entry_from_campaign_ = true;
        took_partial_ = false;
        max_r_reached_ = 0.0;
        trail_armed_ = false;
        bars_held_ = 0;

        state_ = ImpulsePositionState::OPEN;
        trades_today_++;

        // Send order
        if (order_callback_) {
            order_callback_("XAUUSD", side > 0, size);
        }

        printf("[IMPULSE-A] ENTRY %s @ %.2f stop=%.2f size=%.2f risk=$%.2f conf=%.2f\n",
               side > 0 ? "LONG" : "SHORT", pos_.entry, pos_.stop, pos_.size,
               risk_dollars, ctx.confidence);
    }

    // -------------------------------------------------------------------------
    // POSITION MANAGEMENT
    // -------------------------------------------------------------------------
    void managePosition() {
        if (state_ == ImpulsePositionState::FLAT) return;

        const auto& ctx = campaign_.getCampaign();
        
        // Track max R
        double r = currentR();
        if (r > max_r_reached_) {
            max_r_reached_ = r;
        }

        // =====================================================================
        // CAMPAIGN INVALIDATION = IMMEDIATE EXIT (REGIME flip)
        // =====================================================================
        if (ctx.state == CampaignState::INVALIDATED) {
            printf("[IMPULSE-A] Campaign invalidated - REGIME exit\n");
            exitAll("REGIME");
            return;
        }

        // =====================================================================
        // HARD STOP CHECK
        // =====================================================================
        if (pos_.side > 0 && last_low_ <= pos_.stop) {
            exitAll("STOP");
            return;
        }
        if (pos_.side < 0 && last_high_ >= pos_.stop) {
            exitAll("STOP");
            return;
        }

        // =====================================================================
        // PARTIAL AT +1R (validated)
        // =====================================================================
        if (state_ == ImpulsePositionState::OPEN && r >= PARTIAL_R) {
            takePartial();
        }

        // =====================================================================
        // STRUCTURAL TRAIL (arms at +1.5R, validated)
        // =====================================================================
        if (max_r_reached_ >= TRAIL_ARM_R) {
            trail_armed_ = true;
            updateTrail();
            
            // Check trail hit
            if (trailHit()) {
                exitAll("TRAIL");
                return;
            }
        }

        // =====================================================================
        // TIME EXIT
        // =====================================================================
        int max_bars = MAX_HOLD_BARS;
        if (ctx.confidence >= EXTENSION_CONF) {
            max_bars += EXTENSION_BARS;  // +10 min for high confidence
        }
        
        if (bars_held_ >= max_bars) {
            exitAll("TIME");
            return;
        }
    }

    // -------------------------------------------------------------------------
    // PARTIAL LOGIC (validated: 40% at +1R)
    // -------------------------------------------------------------------------
    void takePartial() {
        double partial_size = pos_.size * PARTIAL_PCT;  // 40%
        double runner_size = pos_.size - partial_size;   // 60%
        
        took_partial_ = true;
        
        // Send partial exit order
        if (order_callback_) {
            order_callback_("XAUUSD", pos_.side < 0, partial_size);
        }

        // Record partial trade
        double pnl_points = (pos_.side > 0)
            ? (last_price_ - pos_.entry)
            : (pos_.entry - last_price_);
        double pnl_dollars = pnl_points * partial_size * 100.0;
        
        ImpulseTradeRecord record;
        std::strncpy(record.symbol, "XAUUSD", sizeof(record.symbol) - 1);
        record.side = pos_.side;
        record.entry_price = pos_.entry;
        record.exit_price = last_price_;
        record.size = partial_size;
        record.pnl_dollars = pnl_dollars;
        record.pnl_r = PARTIAL_R * PARTIAL_PCT;  // 0.4R realized
        std::strncpy(record.exit_reason, "PARTIAL", sizeof(record.exit_reason) - 1);
        record.is_partial = true;
        record.entry_ts = pos_.entry_ts;
        record.exit_ts = nowNs();
        
        if (trade_callback_) {
            trade_callback_(record);
        }

        // Move stop to BREAKEVEN (no buffer - validated)
        pos_.stop = pos_.entry;
        pos_.remaining_size = runner_size;
        
        state_ = ImpulsePositionState::PARTIAL;

        printf("[IMPULSE-A] PARTIAL @ %.2f: exited %.2f lots (+0.4R), runner=%.2f, stop→BE\n",
               last_price_, partial_size, runner_size);
    }

    // -------------------------------------------------------------------------
    // STRUCTURAL TRAIL (campaign geometry based)
    // -------------------------------------------------------------------------
    void updateTrail() {
        if (!trail_armed_) return;
        
        double range = pos_.observe_high - pos_.observe_low;
        if (range <= 0) return;

        if (pos_.side > 0) {
            // LONG: trail at 50% of campaign range from low
            double new_trail = pos_.observe_low + TRAIL_FRAC * range;
            if (new_trail > pos_.trail) {
                pos_.trail = new_trail;
            }
        } else {
            // SHORT: trail at 50% of campaign range from high
            double new_trail = pos_.observe_high - TRAIL_FRAC * range;
            if (new_trail < pos_.trail || pos_.trail == pos_.stop) {
                pos_.trail = new_trail;
            }
        }
    }

    bool trailHit() const {
        if (!trail_armed_) return false;
        
        if (pos_.side > 0) {
            return last_low_ <= pos_.trail && pos_.trail > pos_.stop;
        } else {
            return last_high_ >= pos_.trail && pos_.trail < pos_.stop;
        }
    }

    // -------------------------------------------------------------------------
    // EXIT ALL
    // -------------------------------------------------------------------------
    void exitAll(const char* reason) {
        if (state_ == ImpulsePositionState::FLAT) return;

        double exit_size = pos_.remaining_size;
        
        // Send close order
        if (order_callback_ && exit_size > 0.0) {
            order_callback_("XAUUSD", pos_.side < 0, exit_size);
        }

        // Calculate final PnL
        double r = currentR();
        double pnl_points = (pos_.side > 0)
            ? (last_price_ - pos_.entry)
            : (pos_.entry - last_price_);
        double pnl_dollars = pnl_points * exit_size * 100.0;
        
        // Calculate total R for this trade
        double total_r = 0.0;
        if (strcmp(reason, "STOP") == 0) {
            if (took_partial_) {
                // Already took 0.4R, remainder at BE = 0
                total_r = PARTIAL_R * PARTIAL_PCT;  // +0.4R
            } else {
                // Full stop loss
                total_r = -1.0;
            }
        } else {
            // Non-stop exit: partial + remaining
            if (took_partial_) {
                double remaining_frac = 1.0 - PARTIAL_PCT;  // 60%
                total_r = (PARTIAL_R * PARTIAL_PCT) + (r * remaining_frac);
            } else {
                total_r = r;
            }
        }
        
        // Detect runner_failed
        bool runner_failed = (max_r_reached_ >= 1.0) && (total_r < 0.0);

        // Record trade
        ImpulseTradeRecord record;
        std::strncpy(record.symbol, "XAUUSD", sizeof(record.symbol) - 1);
        record.side = pos_.side;
        record.entry_price = pos_.entry;
        record.exit_price = last_price_;
        record.size = exit_size;
        record.pnl_dollars = pnl_dollars;
        record.pnl_r = total_r;
        std::strncpy(record.exit_reason, reason, sizeof(record.exit_reason) - 1);
        record.is_partial = false;
        record.entry_ts = pos_.entry_ts;
        record.exit_ts = nowNs();

        if (trade_callback_) {
            trade_callback_(record);
        }
        
        // Report to scale guard
        if (scale_stats_callback_) {
            scale_stats_callback_(total_r, entry_from_campaign_, took_partial_, runner_failed);
        }

        printf("[IMPULSE-A] EXIT %s @ %.2f reason=%s size=%.2f pnl=$%.2f (%.2fR) max_r=%.2f\n",
               pos_.side > 0 ? "LONG" : "SHORT", last_price_, reason,
               exit_size, pnl_dollars, total_r, max_r_reached_);

        // Reset state
        state_ = ImpulsePositionState::FLAT;
        pos_ = ImpulsePosition{};
        entry_from_campaign_ = false;
        took_partial_ = false;
        max_r_reached_ = 0.0;
        trail_armed_ = false;
        bars_held_ = 0;
    }

    // -------------------------------------------------------------------------
    // UTILITIES
    // -------------------------------------------------------------------------
    double currentR() const {
        if (pos_.initial_risk <= 0.0) return 0.0;
        
        double pnl = (pos_.side > 0)
            ? (last_price_ - pos_.entry)
            : (pos_.entry - last_price_);
            
        return pnl / pos_.initial_risk;
    }

    double campaignRange() const {
        const auto& ctx = campaign_.getCampaign();
        double range = ctx.observe_high - ctx.observe_low;
        return std::max(15.0, range);
    }
    
    uint64_t nowNs() const {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }
};

} // namespace gold
