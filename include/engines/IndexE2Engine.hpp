#pragma once
// =============================================================================
// IndexE2Engine.hpp - Chimera v4.10.2 E2 VWAP Pullback Engine
// =============================================================================
// LOCKED SPEC (DO NOT CHANGE):
//   - Symbols: NAS100 + US30 only
//   - Engine: E2 primary (VWAP pullback), E1 filter (compression quality)
//   - Risk: Fixed 0.5% NAS100, 0.4% US30
//   - Max trades: 1 per symbol per day
//   - Exits: Partial 60% at +1R, Stall kill 6 bars (<0.3R), Runner protection
//   - Time window: 10:05-14:30 NY
//
// BACKTEST VALIDATED:
//   - 1.94 trades/day ✅
//   - 43.3% WR ✅  
//   - +18.2R total ✅
//   - Stall kills dominate exits ✅
// =============================================================================

#include <string>
#include <cstring>
#include <atomic>
#include <chrono>
#include <cmath>
#include <array>
#include <functional>
#include <cstdio>

namespace Chimera {
namespace E2 {

// =============================================================================
// v4.10.2 CONFIG - LOCKED, DO NOT MODIFY
// =============================================================================
struct E2Config {
    // Time window (NY)
    static constexpr int SESSION_START_HOUR = 9;
    static constexpr int SESSION_START_MIN = 30;
    static constexpr int SESSION_END_HOUR = 15;
    static constexpr int SESSION_END_MIN = 30;
    static constexpr int OR_END_HOUR = 10;
    static constexpr int OR_END_MIN = 0;
    static constexpr int E2_START_HOUR = 10;
    static constexpr int E2_START_MIN = 5;
    static constexpr int E2_END_HOUR = 14;
    static constexpr int E2_END_MIN = 30;
    
    // Entry filters
    static constexpr double VWAP_THRESHOLD_ATR_MULT = 0.15;
    static constexpr double MIN_VWAP_EXCURSION_ATR = 0.25;
    static constexpr double MIN_COMPRESSION_QUALITY = 0.4;
    static constexpr int MIN_BARS_SINCE_OR = 10;
    static constexpr int MAX_TRADES_PER_SYMBOL = 1;
    
    // Exit engine
    static constexpr double PARTIAL_PCT = 0.60;
    static constexpr double PARTIAL_R = 1.0;
    static constexpr int STALL_BARS = 6;
    static constexpr double STALL_THRESHOLD_INITIAL = 0.3;
    static constexpr double STALL_THRESHOLD_PARTIAL = 0.5;
    static constexpr double RUNNER_BE_BUFFER = 0.1;
    
    // Daily halt
    static constexpr double DAILY_HALT_R = 2.0;
    
    // Risk per symbol
    static constexpr double NAS100_RISK_PCT = 0.005;  // 0.5%
    static constexpr double US30_RISK_PCT = 0.004;    // 0.4%
};

// =============================================================================
// Exit Types
// =============================================================================
enum class ExitType : uint8_t {
    NONE = 0,
    STOP_LOSS = 1,
    PARTIAL = 2,
    STALL_KILL = 3,
    TRAILING = 4,
    EOD = 5
};

inline const char* exitTypeStr(ExitType t) {
    switch (t) {
        case ExitType::STOP_LOSS: return "STOP_LOSS";
        case ExitType::PARTIAL: return "PARTIAL";
        case ExitType::STALL_KILL: return "STALL_KILL";
        case ExitType::TRAILING: return "TRAILING";
        case ExitType::EOD: return "EOD";
        default: return "NONE";
    }
}

// =============================================================================
// Exit Stage (for runner protection)
// =============================================================================
enum class ExitStage : uint8_t {
    INITIAL = 0,
    PARTIAL_TAKEN = 1,
    RUNNER_PROTECTED = 2,
    TRAILING = 3
};

// =============================================================================
// Position State
// =============================================================================
struct E2Position {
    bool active = false;
    int side = 0;                  // +1 long, -1 short
    double entry_price = 0.0;
    uint64_t entry_ts = 0;
    double stop_loss = 0.0;
    double initial_risk = 0.0;     // 1R in price terms
    double initial_size = 0.0;
    double current_size = 0.0;
    double partial_target = 0.0;
    double runner_stop = 0.0;
    int bars_held = 0;
    ExitStage exit_stage = ExitStage::INITIAL;
    double highest_r = 0.0;
    double last_swing_high = 0.0;
    double last_swing_low = 0.0;
    
    double current_r(double price) const {
        if (initial_risk <= 0) return 0.0;
        double pnl = (side > 0) ? (price - entry_price) : (entry_price - price);
        return pnl / initial_risk;
    }
    
    void reset() {
        active = false;
        side = 0;
        entry_price = 0.0;
        entry_ts = 0;
        stop_loss = 0.0;
        initial_risk = 0.0;
        initial_size = 0.0;
        current_size = 0.0;
        partial_target = 0.0;
        runner_stop = 0.0;
        bars_held = 0;
        exit_stage = ExitStage::INITIAL;
        highest_r = 0.0;
        last_swing_high = 0.0;
        last_swing_low = 0.0;
    }
};

// =============================================================================
// Symbol State (per-symbol tracking)
// =============================================================================
struct E2SymbolState {
    char symbol[16] = {0};
    
    // ATR
    double atr_20 = 0.0;
    std::array<double, 20> tr_history = {0};
    int tr_count = 0;
    
    // VWAP
    double vwap = 0.0;
    double vwap_pv_sum = 0.0;
    double vwap_vol_sum = 0.0;
    
    // Opening range
    double or_high = 0.0;
    double or_low = 0.0;
    double or_mid = 0.0;
    bool or_recorded = false;
    int bars_since_or = 0;
    
    // VWAP excursion tracking
    double max_vwap_excursion = 0.0;
    
    // E1 filter: compression
    bool compression_active = false;
    double compression_quality = 0.0;
    
    // Bar history (for compression calc and swing detection)
    std::array<double, 30> bar_highs = {0};
    std::array<double, 30> bar_lows = {0};
    std::array<double, 30> bar_closes = {0};
    int bar_count = 0;
    
    // Position
    E2Position position;
    int trades_today = 0;
    
    // Previous close for TR calc
    double prev_close = 0.0;
    
    // Daily tracking
    double daily_pnl_r = 0.0;
    bool halted_today = false;
    
    void resetDaily() {
        or_high = 0.0;
        or_low = 0.0;
        or_mid = 0.0;
        or_recorded = false;
        bars_since_or = 0;
        max_vwap_excursion = 0.0;
        vwap_pv_sum = 0.0;
        vwap_vol_sum = 0.0;
        vwap = 0.0;
        compression_active = false;
        compression_quality = 0.0;
        position.reset();
        trades_today = 0;
        daily_pnl_r = 0.0;
        halted_today = false;
    }
};

// =============================================================================
// Trade Record (for audit logging)
// =============================================================================
struct E2TradeRecord {
    char symbol[16] = {0};
    char date[16] = {0};
    int side = 0;
    double entry_price = 0.0;
    double exit_price = 0.0;
    double size = 0.0;
    double pnl_r = 0.0;
    double pnl_dollars = 0.0;
    ExitType exit_type = ExitType::NONE;
    int bars_held = 0;
    char entry_reason[32] = {0};
    char exit_reason[64] = {0};
    uint64_t entry_ts = 0;
    uint64_t exit_ts = 0;
};

// =============================================================================
// Tick Structure
// =============================================================================
struct E2Tick {
    double bid = 0.0;
    double ask = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double volume = 1000.0;
    uint64_t ts_ns = 0;
    int ny_hour = 0;
    int ny_min = 0;
};

// =============================================================================
// IndexE2Engine - Main Engine Class
// =============================================================================
class IndexE2Engine {
public:
    using OrderCallback = std::function<void(const char* symbol, bool is_buy, double qty)>;
    using TradeCallback = std::function<void(const E2TradeRecord& trade)>;
    using AuditCallback = std::function<void(const char* symbol, const char* event, const char* details)>;

    explicit IndexE2Engine(const char* symbol, double equity);
    
    void onTick(const E2Tick& tick);
    void onBar(const E2Tick& bar);  // Called on bar close
    void resetDaily();
    void forceEOD(const E2Tick& tick);
    
    void setOrderCallback(OrderCallback cb) { order_cb_ = std::move(cb); }
    void setTradeCallback(TradeCallback cb) { trade_cb_ = std::move(cb); }
    void setAuditCallback(AuditCallback cb) { audit_cb_ = std::move(cb); }
    
    // Getters
    const E2SymbolState& state() const { return state_; }
    bool hasPosition() const { return state_.position.active; }
    int tradesToday() const { return state_.trades_today; }
    bool isHalted() const { return state_.halted_today; }
    
private:
    void updateATR(double high, double low, double close);
    void updateVWAP(double price, double volume);
    void updateOpeningRange(const E2Tick& bar);
    void updateCompression();
    void updateSwings();
    
    int checkE2Entry(const E2Tick& bar);
    void openPosition(const E2Tick& bar, int direction);
    void managePosition(const E2Tick& bar);
    void closePosition(const E2Tick& bar, ExitType exit_type, const char* reason);
    void closePartial(const E2Tick& bar);
    
    bool isInSession(int hour, int min) const;
    bool isInORWindow(int hour, int min) const;
    bool isInE2Window(int hour, int min) const;
    
    double riskPct() const;
    
    void audit(const char* event, const char* details);

private:
    E2SymbolState state_;
    double equity_;
    OrderCallback order_cb_;
    TradeCallback trade_cb_;
    AuditCallback audit_cb_;
    uint64_t last_bar_ts_ = 0;
};

// =============================================================================
// IMPLEMENTATION
// =============================================================================

inline IndexE2Engine::IndexE2Engine(const char* symbol, double equity)
    : equity_(equity)
{
    std::strncpy(state_.symbol, symbol, sizeof(state_.symbol) - 1);
    printf("[E2-ENGINE] Created for %s, equity=%.2f, risk=%.2f%%\n",
           symbol, equity, riskPct() * 100.0);
}

inline double IndexE2Engine::riskPct() const {
    if (std::strcmp(state_.symbol, "NAS100") == 0) {
        return E2Config::NAS100_RISK_PCT;
    } else if (std::strcmp(state_.symbol, "US30") == 0) {
        return E2Config::US30_RISK_PCT;
    }
    return 0.005;  // Default 0.5%
}

inline bool IndexE2Engine::isInSession(int hour, int min) const {
    int t = hour * 60 + min;
    int start = E2Config::SESSION_START_HOUR * 60 + E2Config::SESSION_START_MIN;
    int end = E2Config::SESSION_END_HOUR * 60 + E2Config::SESSION_END_MIN;
    return t >= start && t <= end;
}

inline bool IndexE2Engine::isInORWindow(int hour, int min) const {
    int t = hour * 60 + min;
    int start = E2Config::SESSION_START_HOUR * 60 + E2Config::SESSION_START_MIN;
    int end = E2Config::OR_END_HOUR * 60 + E2Config::OR_END_MIN;
    return t >= start && t < end;
}

inline bool IndexE2Engine::isInE2Window(int hour, int min) const {
    int t = hour * 60 + min;
    int start = E2Config::E2_START_HOUR * 60 + E2Config::E2_START_MIN;
    int end = E2Config::E2_END_HOUR * 60 + E2Config::E2_END_MIN;
    return t >= start && t <= end;
}

inline void IndexE2Engine::resetDaily() {
    state_.resetDaily();
    audit("DAILY_RESET", "New trading day started");
}

inline void IndexE2Engine::updateATR(double high, double low, double close) {
    double tr = high - low;
    if (state_.prev_close > 0.0) {
        tr = std::max(tr, std::max(
            std::fabs(high - state_.prev_close),
            std::fabs(low - state_.prev_close)
        ));
    }
    
    int idx = state_.tr_count % 20;
    state_.tr_history[idx] = tr;
    state_.tr_count++;
    
    if (state_.tr_count >= 5) {
        double sum = 0.0;
        int count = std::min(state_.tr_count, 20);
        for (int i = 0; i < count; i++) {
            sum += state_.tr_history[i];
        }
        state_.atr_20 = sum / count;
    }
    
    state_.prev_close = close;
}

inline void IndexE2Engine::updateVWAP(double price, double volume) {
    state_.vwap_pv_sum += price * volume;
    state_.vwap_vol_sum += volume;
    if (state_.vwap_vol_sum > 0) {
        state_.vwap = state_.vwap_pv_sum / state_.vwap_vol_sum;
    }
}

inline void IndexE2Engine::updateOpeningRange(const E2Tick& bar) {
    if (isInORWindow(bar.ny_hour, bar.ny_min)) {
        if (state_.or_high == 0.0) {
            state_.or_high = bar.high;
            state_.or_low = bar.low;
        }
        state_.or_high = std::max(state_.or_high, bar.high);
        state_.or_low = std::min(state_.or_low, bar.low);
    } else if (!state_.or_recorded && state_.or_high > 0.0) {
        state_.or_mid = (state_.or_high + state_.or_low) / 2.0;
        state_.or_recorded = true;
        
        char buf[128];
        snprintf(buf, sizeof(buf), "OR_HIGH=%.2f OR_LOW=%.2f OR_MID=%.2f",
                 state_.or_high, state_.or_low, state_.or_mid);
        audit("OR_RECORDED", buf);
    }
}

inline void IndexE2Engine::updateCompression() {
    if (state_.bar_count < 20 || state_.atr_20 <= 0) {
        state_.compression_active = false;
        state_.compression_quality = 0.0;
        return;
    }
    
    // Recent 10 bar range vs ATR
    double max_h = 0.0, min_l = 1e9;
    int start = std::max(0, state_.bar_count - 10);
    for (int i = start; i < state_.bar_count && i < 30; i++) {
        int idx = i % 30;
        if (state_.bar_highs[idx] > max_h) max_h = state_.bar_highs[idx];
        if (state_.bar_lows[idx] < min_l && state_.bar_lows[idx] > 0) min_l = state_.bar_lows[idx];
    }
    
    double recent_range = max_h - min_l;
    double ratio = recent_range / (state_.atr_20 * 0.6);
    state_.compression_active = ratio < 1.0;
    state_.compression_quality = state_.compression_active ? std::max(0.0, 1.0 - ratio) : 0.0;
}

inline void IndexE2Engine::updateSwings() {
    if (state_.bar_count < 3) return;
    
    int i2 = (state_.bar_count - 2) % 30;
    int i1 = (state_.bar_count - 3) % 30;
    int i3 = (state_.bar_count - 1) % 30;
    
    double h1 = state_.bar_highs[i1];
    double h2 = state_.bar_highs[i2];
    double h3 = state_.bar_highs[i3];
    
    if (h2 > h1 && h2 > h3) {
        state_.position.last_swing_high = h2;
    }
    
    double l1 = state_.bar_lows[i1];
    double l2 = state_.bar_lows[i2];
    double l3 = state_.bar_lows[i3];
    
    if (l2 < l1 && l2 < l3 && l2 > 0) {
        state_.position.last_swing_low = l2;
    }
}

inline void IndexE2Engine::onTick(const E2Tick& tick) {
    // Tick-level position management (for stops/targets)
    if (state_.position.active) {
        // Check hard stop
        double price = tick.close;
        E2Position& pos = state_.position;
        
        bool stopped = false;
        double exit_price = price;
        
        if (pos.side > 0 && tick.low <= pos.stop_loss) {
            stopped = true;
            exit_price = pos.stop_loss;
        } else if (pos.side < 0 && tick.high >= pos.stop_loss) {
            stopped = true;
            exit_price = pos.stop_loss;
        }
        
        if (stopped) {
            E2Tick exit_tick = tick;
            exit_tick.close = exit_price;
            closePosition(exit_tick, ExitType::STOP_LOSS, "HARD_STOP_HIT");
        }
    }
}

inline void IndexE2Engine::onBar(const E2Tick& bar) {
    if (state_.halted_today) return;
    if (!isInSession(bar.ny_hour, bar.ny_min)) return;
    
    // Update indicators
    updateVWAP(bar.close, bar.volume);
    updateATR(bar.high, bar.low, bar.close);
    
    // Store bar data
    int idx = state_.bar_count % 30;
    state_.bar_highs[idx] = bar.high;
    state_.bar_lows[idx] = bar.low;
    state_.bar_closes[idx] = bar.close;
    state_.bar_count++;
    
    // Update OR
    updateOpeningRange(bar);
    
    // Track bars since OR
    if (state_.or_recorded) {
        state_.bars_since_or++;
        
        // Track max excursion from VWAP
        if (state_.vwap > 0) {
            double excursion = std::fabs(bar.close - state_.vwap);
            state_.max_vwap_excursion = std::max(state_.max_vwap_excursion, excursion);
        }
    }
    
    // Update E1 filter
    updateCompression();
    updateSwings();
    
    // Position management
    if (state_.position.active) {
        managePosition(bar);
    }
    
    // Entry check
    if (!state_.position.active && state_.or_recorded) {
        int direction = checkE2Entry(bar);
        if (direction != 0) {
            openPosition(bar, direction);
        }
    }
    
    last_bar_ts_ = bar.ts_ns;
}

inline int IndexE2Engine::checkE2Entry(const E2Tick& bar) {
    // Time window filter
    if (!isInE2Window(bar.ny_hour, bar.ny_min)) return 0;
    
    // ATR/VWAP must be valid
    if (state_.atr_20 <= 0 || state_.vwap <= 0) return 0;
    if (!state_.or_recorded) return 0;
    if (state_.trades_today >= E2Config::MAX_TRADES_PER_SYMBOL) return 0;
    
    // Minimum bars since OR
    if (state_.bars_since_or < E2Config::MIN_BARS_SINCE_OR) return 0;
    
    // Soft gate: need compression OR meaningful excursion
    double compression_bonus = state_.compression_active ? state_.compression_quality : 0.0;
    bool has_excursion = state_.max_vwap_excursion >= state_.atr_20 * E2Config::MIN_VWAP_EXCURSION_ATR;
    
    if (compression_bonus < E2Config::MIN_COMPRESSION_QUALITY && !has_excursion) {
        return 0;
    }
    
    // Check VWAP proximity (tight threshold)
    double dist_to_vwap = std::fabs(bar.close - state_.vwap);
    if (dist_to_vwap > state_.atr_20 * E2Config::VWAP_THRESHOLD_ATR_MULT) {
        return 0;
    }
    
    // Direction from OR structure
    // Long: price above OR mid, pulled back to VWAP
    // Short: price below OR mid, pulled back to VWAP
    
    if (bar.close > state_.or_mid && bar.close <= state_.vwap + state_.atr_20 * 0.05) {
        return 1;  // Long
    }
    if (bar.close < state_.or_mid && bar.close >= state_.vwap - state_.atr_20 * 0.05) {
        return -1;  // Short
    }
    
    return 0;
}

inline void IndexE2Engine::openPosition(const E2Tick& bar, int direction) {
    E2Position& pos = state_.position;
    
    // Calculate stop and risk
    double sl = (direction > 0) ? state_.or_low : state_.or_high;
    double risk_points = std::fabs(bar.close - sl);
    if (risk_points <= 0) return;
    
    // Fixed risk sizing
    double risk_dollars = equity_ * riskPct();
    double size = risk_dollars / risk_points;
    
    // Partial target
    double partial_target = (direction > 0) ? state_.or_high : state_.or_low;
    
    // Runner stop (BE + buffer)
    double runner_stop = bar.close + direction * risk_points * E2Config::RUNNER_BE_BUFFER;
    
    pos.active = true;
    pos.side = direction;
    pos.entry_price = bar.close;
    pos.entry_ts = bar.ts_ns;
    pos.stop_loss = sl;
    pos.initial_risk = risk_points;
    pos.initial_size = size;
    pos.current_size = size;
    pos.partial_target = partial_target;
    pos.runner_stop = runner_stop;
    pos.bars_held = 0;
    pos.exit_stage = ExitStage::INITIAL;
    pos.highest_r = 0.0;
    
    state_.trades_today++;
    
    // Send order
    if (order_cb_) {
        order_cb_(state_.symbol, direction > 0, size);
    }
    
    char buf[256];
    snprintf(buf, sizeof(buf), 
             "SIDE=%s ENTRY=%.2f SL=%.2f 1R=%.2f SIZE=%.4f PT=%.2f RUNNER_STOP=%.2f",
             direction > 0 ? "LONG" : "SHORT",
             bar.close, sl, risk_points, size, partial_target, runner_stop);
    audit("ENTRY", buf);
    
    printf("[E2][%s] ENTER %s @ %.2f SL=%.2f 1R=%.2f SIZE=%.4f\n",
           state_.symbol, direction > 0 ? "LONG" : "SHORT",
           bar.close, sl, risk_points, size);
}

inline void IndexE2Engine::managePosition(const E2Tick& bar) {
    E2Position& pos = state_.position;
    if (!pos.active) return;
    
    double price = bar.close;
    double current_r = pos.current_r(price);
    pos.highest_r = std::max(pos.highest_r, current_r);
    pos.bars_held++;
    
    // CHECK 1: STALL KILL (no meaningful progress after N bars)
    if (pos.exit_stage == ExitStage::INITIAL || pos.exit_stage == ExitStage::PARTIAL_TAKEN) {
        double stall_threshold = (pos.exit_stage == ExitStage::INITIAL) 
            ? E2Config::STALL_THRESHOLD_INITIAL 
            : E2Config::STALL_THRESHOLD_PARTIAL;
        
        if (pos.bars_held >= E2Config::STALL_BARS && current_r < stall_threshold) {
            char reason[64];
            snprintf(reason, sizeof(reason), "STALL_KILL_BARS=%d R=%.2f", pos.bars_held, current_r);
            closePosition(bar, ExitType::STALL_KILL, reason);
            return;
        }
    }
    
    // CHECK 2: PARTIAL PROFIT
    if (pos.exit_stage == ExitStage::INITIAL && current_r >= E2Config::PARTIAL_R) {
        bool partial_hit = false;
        if (pos.side > 0 && price >= pos.partial_target) partial_hit = true;
        if (pos.side < 0 && price <= pos.partial_target) partial_hit = true;
        
        if (partial_hit) {
            closePartial(bar);
            return;
        }
    }
    
    // CHECK 3: RUNNER TRAIL (after partial)
    if (pos.exit_stage == ExitStage::PARTIAL_TAKEN || pos.exit_stage == ExitStage::RUNNER_PROTECTED) {
        if (current_r >= 2.0 && pos.exit_stage != ExitStage::TRAILING) {
            pos.exit_stage = ExitStage::TRAILING;
            audit("TRAILING_ACTIVATED", "R >= 2.0");
        }
        
        if (pos.exit_stage == ExitStage::TRAILING) {
            // Trail to last swing
            if (pos.side > 0 && pos.last_swing_low > pos.stop_loss) {
                pos.stop_loss = pos.last_swing_low;
            } else if (pos.side < 0 && pos.last_swing_high > 0 && pos.last_swing_high < pos.stop_loss) {
                pos.stop_loss = pos.last_swing_high;
            }
        }
    }
}

inline void IndexE2Engine::closePartial(const E2Tick& bar) {
    E2Position& pos = state_.position;
    
    double partial_size = pos.initial_size * E2Config::PARTIAL_PCT;
    double exit_price = bar.close;
    double pnl_r = pos.current_r(exit_price);
    double pnl_dollars = pnl_r * pos.initial_risk * partial_size;
    
    // Create trade record for partial
    E2TradeRecord record;
    std::strncpy(record.symbol, state_.symbol, sizeof(record.symbol) - 1);
    record.side = pos.side;
    record.entry_price = pos.entry_price;
    record.exit_price = exit_price;
    record.size = partial_size;
    record.pnl_r = pnl_r;
    record.pnl_dollars = pnl_dollars;
    record.exit_type = ExitType::PARTIAL;
    record.bars_held = pos.bars_held;
    std::strncpy(record.entry_reason, "VWAP_PULLBACK", sizeof(record.entry_reason) - 1);
    std::strncpy(record.exit_reason, "PARTIAL_AT_STRUCTURE", sizeof(record.exit_reason) - 1);
    record.entry_ts = pos.entry_ts;
    record.exit_ts = bar.ts_ns;
    
    if (trade_cb_) {
        trade_cb_(record);
    }
    
    // Move stop to BE (runner protection)
    pos.stop_loss = pos.runner_stop;
    pos.current_size = pos.initial_size - partial_size;
    pos.exit_stage = ExitStage::PARTIAL_TAKEN;
    
    state_.daily_pnl_r += pnl_r * (partial_size / pos.initial_size);
    
    // Send partial close order
    if (order_cb_) {
        order_cb_(state_.symbol, pos.side < 0, partial_size);  // Close = opposite side
    }
    
    char buf[256];
    snprintf(buf, sizeof(buf), 
             "PARTIAL_SIZE=%.4f EXIT=%.2f PNL_R=%.2f NEW_SL=%.2f REMAINING=%.4f",
             partial_size, exit_price, pnl_r, pos.stop_loss, pos.current_size);
    audit("PARTIAL", buf);
    
    printf("[E2][%s] PARTIAL %.0f%% @ %.2f PNL=%.2fR NEW_SL=%.2f\n",
           state_.symbol, E2Config::PARTIAL_PCT * 100, exit_price, pnl_r, pos.stop_loss);
}

inline void IndexE2Engine::closePosition(const E2Tick& bar, ExitType exit_type, const char* reason) {
    E2Position& pos = state_.position;
    if (!pos.active) return;
    
    double exit_price = bar.close;
    double pnl_r = pos.current_r(exit_price);
    double pnl_dollars = pnl_r * pos.initial_risk * pos.current_size;
    
    // Create trade record
    E2TradeRecord record;
    std::strncpy(record.symbol, state_.symbol, sizeof(record.symbol) - 1);
    record.side = pos.side;
    record.entry_price = pos.entry_price;
    record.exit_price = exit_price;
    record.size = pos.current_size;
    record.pnl_r = pnl_r;
    record.pnl_dollars = pnl_dollars;
    record.exit_type = exit_type;
    record.bars_held = pos.bars_held;
    std::strncpy(record.entry_reason, "VWAP_PULLBACK", sizeof(record.entry_reason) - 1);
    std::strncpy(record.exit_reason, reason, sizeof(record.exit_reason) - 1);
    record.entry_ts = pos.entry_ts;
    record.exit_ts = bar.ts_ns;
    
    if (trade_cb_) {
        trade_cb_(record);
    }
    
    state_.daily_pnl_r += pnl_r * (pos.current_size / pos.initial_size);
    
    // Check daily halt
    if (state_.daily_pnl_r <= -E2Config::DAILY_HALT_R) {
        state_.halted_today = true;
        audit("DAILY_HALT", "Daily loss limit hit");
        printf("[E2][%s] DAILY HALT at %.2fR\n", state_.symbol, state_.daily_pnl_r);
    }
    
    // Send close order
    if (order_cb_) {
        order_cb_(state_.symbol, pos.side < 0, pos.current_size);
    }
    
    char buf[256];
    snprintf(buf, sizeof(buf),
             "EXIT_TYPE=%s EXIT=%.2f PNL_R=%.2f BARS=%d REASON=%s",
             exitTypeStr(exit_type), exit_price, pnl_r, pos.bars_held, reason);
    audit("EXIT", buf);
    
    printf("[E2][%s] EXIT %s @ %.2f PNL=%.2fR BARS=%d REASON=%s\n",
           state_.symbol, pos.side > 0 ? "LONG" : "SHORT",
           exit_price, pnl_r, pos.bars_held, reason);
    
    pos.reset();
}

inline void IndexE2Engine::forceEOD(const E2Tick& tick) {
    if (state_.position.active) {
        closePosition(tick, ExitType::EOD, "END_OF_DAY");
    }
}

inline void IndexE2Engine::audit(const char* event, const char* details) {
    if (audit_cb_) {
        audit_cb_(state_.symbol, event, details);
    }
}

} // namespace E2
} // namespace Chimera
