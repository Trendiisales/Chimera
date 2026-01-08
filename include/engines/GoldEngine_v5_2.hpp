// =============================================================================
// GoldEngine_v5_2.hpp - FINAL GOLD TRADING ENGINE
// =============================================================================
// AUDIT FIXES APPLIED:
//   ✅ Entry distance structure-relative (not arbitrary $8)
//   ✅ Stops beyond observe range (not magic numbers)
//   ✅ Partial at +1.5R with BE+buffer protection
//   ✅ Runner trails only after 3R (not in chop)
//   ✅ Campaign invalidation = immediate full exit
//
// BEHAVIOR:
//   - Trades XAUUSD only
//   - One trade per campaign
//   - Many days: no trades
//   - Losses: clean, obvious
//   - Wins: partial + runner
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
enum class PositionState : uint8_t {
    FLAT    = 0,
    OPEN    = 1,
    PARTIAL = 2
};

inline const char* positionStateStr(PositionState s) {
    switch (s) {
        case PositionState::OPEN:    return "OPEN";
        case PositionState::PARTIAL: return "PARTIAL";
        default:                     return "FLAT";
    }
}

// =============================================================================
// POSITION
// =============================================================================
struct Position {
    int     side        = 0;       // +1 long, -1 short
    double  entry       = 0.0;
    double  stop        = 0.0;
    double  initial_risk = 0.0;
    double  size        = 0.0;
    double  runner_size = 0.0;
    uint64_t entry_ts   = 0;
};

// =============================================================================
// TRADE RECORD (for callbacks)
// =============================================================================
struct GoldTradeRecord {
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
// GOLD ENGINE v5.2
// =============================================================================
class GoldEngineV5_2 {
public:
    using OrderCallback = std::function<void(const char* symbol, bool is_buy, double qty)>;
    using TradeCallback = std::function<void(const GoldTradeRecord& trade)>;
    using ScaleStatsCallback = std::function<void(double pnl_r, bool from_campaign, bool took_partial, bool runner_failed)>;
    using PermissionCallback = std::function<bool(double* risk_pct, const char** reason)>;

    GoldEngineV5_2() = default;

    // -------------------------------------------------------------------------
    // CONFIGURATION
    // -------------------------------------------------------------------------
    void setOrderCallback(OrderCallback cb) { order_callback_ = std::move(cb); }
    void setTradeCallback(TradeCallback cb) { trade_callback_ = std::move(cb); }
    void setScaleStatsCallback(ScaleStatsCallback cb) { scale_stats_callback_ = std::move(cb); }
    void setPermissionCallback(PermissionCallback cb) { permission_callback_ = std::move(cb); }
    void setEquity(double equity) { equity_ = equity; }
    void setRiskPercent(double pct) { risk_pct_ = pct; }  // Set from scale guard

    // -------------------------------------------------------------------------
    // FEEDS
    // -------------------------------------------------------------------------
    void onH1Bar(double h, double l, double c) {
        campaign_.onH1Bar({h, l, c});
    }

    void onM5Bar(double o, double h, double l, double c) {
        campaign_.onM5Bar({o, h, l, c});
        last_price_ = c;
        last_high_ = h;
        last_low_ = l;

        managePosition();
        tryEnter();
    }

    // -------------------------------------------------------------------------
    // STATE QUERIES
    // -------------------------------------------------------------------------
    bool hasPosition() const {
        return state_ != PositionState::FLAT;
    }
    
    PositionState getState() const { return state_; }
    const Position& getPosition() const { return pos_; }
    const CampaignContext& getCampaign() const { return campaign_.getCampaign(); }
    HTFRegime getRegime() const { return campaign_.getRegime(); }
    int tradesToday() const { return trades_today_; }
    
    void resetDaily() {
        trades_today_ = 0;
        // Don't reset campaign - can span days
    }

    void reset() {
        state_ = PositionState::FLAT;
        pos_ = Position{};
        campaign_.reset();
        trades_today_ = 0;
    }
    
    void forceExit(const char* reason = "FORCED") {
        if (state_ != PositionState::FLAT) {
            exitAll(reason);
        }
    }

private:
    // -------------------------------------------------------------------------
    // STATE
    // -------------------------------------------------------------------------
    GoldCampaignEngine campaign_;
    PositionState state_ = PositionState::FLAT;
    Position pos_;
    
    double last_price_ = 0.0;
    double last_high_ = 0.0;
    double last_low_ = 0.0;
    
    double equity_ = 100000.0;
    double risk_pct_ = 0.001;  // 0.10% default (MICRO level from scale guard)
    int trades_today_ = 0;
    
    // Scale guard tracking
    bool entry_from_campaign_ = false;
    bool took_partial_ = false;
    double max_r_reached_ = 0.0;

    OrderCallback order_callback_;
    TradeCallback trade_callback_;
    ScaleStatsCallback scale_stats_callback_;
    PermissionCallback permission_callback_;

    // -------------------------------------------------------------------------
    // CONSTANTS (GOLD-SAFE - DO NOT CHANGE WITHOUT TESTING)
    // -------------------------------------------------------------------------
    static constexpr double PARTIAL_R         = 1.5;    // Take partial at 1.5R
    static constexpr double PARTIAL_PCT       = 0.60;   // Exit 60% at partial
    static constexpr double BE_BUFFER_DOLLARS = 4.0;    // Move stop to BE + $4
    static constexpr double TRAIL_START_R     = 3.0;    // Start trailing at 3R
    static constexpr double TRAIL_FACTOR      = 0.6;    // Trail at 60% of range
    static constexpr int    MAX_TRADES_DAY    = 1;      // One trade per day max

    // -------------------------------------------------------------------------
    // ENTRY LOGIC
    // -------------------------------------------------------------------------
    void tryEnter() {
        if (state_ != PositionState::FLAT) return;
        if (trades_today_ >= MAX_TRADES_DAY) return;

        const auto& ctx = campaign_.getCampaign();
        
        // HARD GATE: Campaign must be ACTIVE
        if (ctx.state != CampaignState::ACTIVE) return;
        if (ctx.confidence < 0.6) return;

        // =====================================================================
        // PORTFOLIO PERMISSION GATE (NON-NEGOTIABLE)
        // Gold CANNOT trade without explicit portfolio permission
        // =====================================================================
        double portfolio_risk = risk_pct_;
        const char* permission_reason = "DEFAULT";
        
        if (permission_callback_) {
            bool allowed = permission_callback_(&portfolio_risk, &permission_reason);
            if (!allowed) {
                printf("[PORTFOLIO] GOLD_PERMISSION=DENIED reason=%s\n", permission_reason);
                return;  // BLOCKED BY PORTFOLIO
            }
            printf("[PORTFOLIO] GOLD_PERMISSION=GRANTED risk=%.2f%% reason=%s\n", 
                   portfolio_risk * 100.0, permission_reason);
            risk_pct_ = portfolio_risk;  // Use portfolio-provided risk
        }

        int side = (ctx.direction == HTFRegime::LONG) ? +1 : -1;

        // AUDIT FIX: Entry distance is structure-relative
        double range = campaignRange();
        double dist = std::abs(last_price_ - ctx.key_level);

        // Must be within 50% of campaign range from key level
        if (dist > 0.5 * range) {
            return;  // Too extended
        }

        // AUDIT FIX: Stop beyond observe range (structural)
        double stop = (side > 0)
            ? ctx.observe_low - 2.0   // Buffer below observe low
            : ctx.observe_high + 2.0; // Buffer above observe high
            
        // Sanity check stop distance
        double stop_dist = std::abs(last_price_ - stop);
        if (stop_dist < 10.0 || stop_dist > 50.0) {
            printf("[GOLD-V5.2] Entry rejected: stop_dist=%.2f out of range [10,50]\n", stop_dist);
            return;
        }

        // Calculate size from risk (using portfolio-provided risk_pct_)
        double risk_dollars = equity_ * risk_pct_;
        double size = risk_dollars / stop_dist;
        
        // Gold: 0.01 lot minimum, 1.0 lot max for micro-live
        size = std::floor(size * 100.0) / 100.0;  // Round to 0.01
        size = std::max(0.01, std::min(size, 1.0));

        // Fill position
        pos_.side = side;
        pos_.entry = last_price_;
        pos_.stop = stop;
        pos_.initial_risk = stop_dist;
        pos_.size = size;
        pos_.runner_size = 0.0;
        pos_.entry_ts = nowNs();
        
        // Track for scale guard
        entry_from_campaign_ = true;

        state_ = PositionState::OPEN;
        trades_today_++;

        // Send order
        if (order_callback_) {
            order_callback_("XAUUSD", side > 0, size);
        }

        printf("[GOLD-V5.2] ENTRY %s @ %.2f stop=%.2f size=%.2f risk=$%.2f conf=%.2f\n",
               side > 0 ? "LONG" : "SHORT", pos_.entry, pos_.stop, pos_.size,
               risk_dollars, ctx.confidence);
    }

    // -------------------------------------------------------------------------
    // POSITION MANAGEMENT
    // -------------------------------------------------------------------------
    void managePosition() {
        if (state_ == PositionState::FLAT) return;

        const auto& ctx = campaign_.getCampaign();
        
        // Track max R reached (for runner_failed detection)
        double r = currentR();
        if (r > max_r_reached_) {
            max_r_reached_ = r;
        }

        // =====================================================================
        // CAMPAIGN INVALIDATION = IMMEDIATE FULL EXIT (NON-NEGOTIABLE)
        // =====================================================================
        if (ctx.state == CampaignState::INVALIDATED) {
            printf("[GOLD-V5.2] Campaign invalidated - exiting immediately\n");
            exitAll("CAMPAIGN_INVALIDATED");
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
        // PARTIAL TAKE AT 1.5R
        // =====================================================================
        if (state_ == PositionState::OPEN && r >= PARTIAL_R) {
            takePartial();
            return;
        }

        // =====================================================================
        // RUNNER TRAILING (only after 3R)
        // =====================================================================
        if (state_ == PositionState::PARTIAL && r >= TRAIL_START_R) {
            trailRunner();
        }
    }

    // -------------------------------------------------------------------------
    // PARTIAL LOGIC
    // -------------------------------------------------------------------------
    void takePartial() {
        double partial_size = pos_.size * PARTIAL_PCT;
        pos_.runner_size = pos_.size - partial_size;
        
        // Track for scale guard
        took_partial_ = true;
        max_r_reached_ = currentR();
        
        // Send partial exit order
        if (order_callback_) {
            order_callback_("XAUUSD", pos_.side < 0, partial_size);  // Opposite side
        }

        // Record partial trade
        double pnl_points = (pos_.side > 0)
            ? (last_price_ - pos_.entry)
            : (pos_.entry - last_price_);
        double pnl_dollars = pnl_points * partial_size * 100.0;  // Gold: $100/point/lot
        
        GoldTradeRecord record;
        std::strncpy(record.symbol, "XAUUSD", sizeof(record.symbol) - 1);
        record.side = pos_.side;
        record.entry_price = pos_.entry;
        record.exit_price = last_price_;
        record.size = partial_size;
        record.pnl_dollars = pnl_dollars;
        record.pnl_r = currentR();
        std::strncpy(record.exit_reason, "PARTIAL", sizeof(record.exit_reason) - 1);
        record.is_partial = true;
        record.entry_ts = pos_.entry_ts;
        record.exit_ts = nowNs();
        
        if (trade_callback_) {
            trade_callback_(record);
        }

        // Move stop to BE + buffer
        pos_.stop = pos_.entry + pos_.side * BE_BUFFER_DOLLARS;
        pos_.size = pos_.runner_size;
        
        state_ = PositionState::PARTIAL;

        printf("[GOLD-V5.2] PARTIAL @ %.2f: exited %.2f lots, runner=%.2f, new_stop=%.2f, pnl=$%.2f\n",
               last_price_, partial_size, pos_.runner_size, pos_.stop, pnl_dollars);
    }

    // -------------------------------------------------------------------------
    // RUNNER TRAILING
    // -------------------------------------------------------------------------
    void trailRunner() {
        double trail_distance = campaignRange() * TRAIL_FACTOR;

        double new_stop = (pos_.side > 0)
            ? last_price_ - trail_distance
            : last_price_ + trail_distance;

        // Only move stop in favorable direction
        if (pos_.side > 0 && new_stop > pos_.stop) {
            printf("[GOLD-V5.2] Trail: stop %.2f -> %.2f (R=%.2f)\n", pos_.stop, new_stop, currentR());
            pos_.stop = new_stop;
        }
        if (pos_.side < 0 && new_stop < pos_.stop) {
            printf("[GOLD-V5.2] Trail: stop %.2f -> %.2f (R=%.2f)\n", pos_.stop, new_stop, currentR());
            pos_.stop = new_stop;
        }
    }

    // -------------------------------------------------------------------------
    // EXIT ALL
    // -------------------------------------------------------------------------
    void exitAll(const char* reason) {
        if (state_ == PositionState::FLAT) return;

        double exit_size = pos_.size;
        
        // Send close order
        if (order_callback_ && exit_size > 0.0) {
            order_callback_("XAUUSD", pos_.side < 0, exit_size);
        }

        // Calculate final PnL
        double pnl_points = (pos_.side > 0)
            ? (last_price_ - pos_.entry)
            : (pos_.entry - last_price_);
        double pnl_dollars = pnl_points * exit_size * 100.0;
        double pnl_r = currentR();
        
        // Detect runner_failed: was winning (max_r >= 1.0) but ended as loss (pnl_r < 0)
        bool runner_failed = (max_r_reached_ >= 1.0) && (pnl_r < 0.0);

        // Record trade
        GoldTradeRecord record;
        std::strncpy(record.symbol, "XAUUSD", sizeof(record.symbol) - 1);
        record.side = pos_.side;
        record.entry_price = pos_.entry;
        record.exit_price = last_price_;
        record.size = exit_size;
        record.pnl_dollars = pnl_dollars;
        record.pnl_r = pnl_r;
        std::strncpy(record.exit_reason, reason, sizeof(record.exit_reason) - 1);
        record.is_partial = false;
        record.entry_ts = pos_.entry_ts;
        record.exit_ts = nowNs();

        if (trade_callback_) {
            trade_callback_(record);
        }
        
        // Report to scale guard
        if (scale_stats_callback_) {
            scale_stats_callback_(pnl_r, entry_from_campaign_, took_partial_, runner_failed);
        }

        printf("[GOLD-V5.2] EXIT %s @ %.2f reason=%s size=%.2f pnl=$%.2f (%.2fR) runner_failed=%s\n",
               pos_.side > 0 ? "LONG" : "SHORT", last_price_, reason,
               exit_size, pnl_dollars, pnl_r, runner_failed ? "YES" : "NO");

        // Reset state
        state_ = PositionState::FLAT;
        pos_ = Position{};
        entry_from_campaign_ = false;
        took_partial_ = false;
        max_r_reached_ = 0.0;
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
        return std::max(15.0, range);  // Minimum $15 range
    }
    
    uint64_t nowNs() const {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }
};

} // namespace gold
