// =============================================================================
// XauValidatedEngine.hpp - VALIDATED GOLD TRADING ENGINE
// =============================================================================
//
// v4.17.0: SPLIT INTO 4 SEPARATE ENGINES (NO SHARED STATE)
//
// USE: #include "engines/xau/XauQuadEngine.hpp" for new architecture
//
// NEW ARCHITECTURE (each engine has OWN position, kill switch, stats):
//   1. XauMicroAlphaEngine (Tick)  - PF 1.50 stress - 45 trades
//   2. XauFadeLowEngine    (4H)    - PF 1.94 stress - 41 trades
//   3. XauRangeBreakEngine (4H)    - PF 1.49 stress - 161 trades
//   4. XauVolBreakEngine   (1H)    - PF 1.43 stress - 52 trades
//
// ORCHESTRATOR: XauQuadEngine manages all 4 engines with:
//   - Bar aggregation from tick feed
//   - Master kill switch
//   - Unified order routing
//   - Per-engine and aggregate stats
//
// PARAMETERS ARE FIXED - DO NOT OPTIMIZE
// =============================================================================
#pragma once

// NEW QUAD ENGINE ARCHITECTURE
#include "xau/XauQuadEngine.hpp"

// BACKWARD COMPATIBILITY: Legacy unified engine (deprecated)
// Use xau::XauQuadEngine instead for new code

#include <cmath>
#include <cstdint>
#include <cinttypes>
#include <cstdio>
#include <algorithm>
#include <functional>
#include <chrono>
#include <deque>
#include <array>

namespace xau_validated {

// =============================================================================
// LEGACY UNIFIED ENGINE (DEPRECATED - USE xau::XauQuadEngine)
// =============================================================================
// Keeping for backward compatibility but recommend migrating to XauQuadEngine

// =============================================================================
// SIGNAL SOURCE
// =============================================================================
enum class SignalSource : uint8_t {
    NONE           = 0,
    FADE_LOW       = 1,  // 4H: buy below 20-bar low
    RANGE_BREAK    = 2,  // 4H: buy above 10-bar high
    VOL_BREAK      = 3,  // 1H: buy on 2x range bullish
    MICRO_ALPHA    = 4   // Tick: impulse fade
};

inline const char* signalSourceStr(SignalSource s) {
    switch (s) {
        case SignalSource::FADE_LOW:     return "FADE_LOW";
        case SignalSource::RANGE_BREAK:  return "RANGE_BREAK";
        case SignalSource::VOL_BREAK:    return "VOL_BREAK";
        case SignalSource::MICRO_ALPHA:  return "MICRO_ALPHA";
        default:                         return "NONE";
    }
}

// =============================================================================
// BAR DATA
// =============================================================================
struct Bar {
    double open  = 0.0;
    double high  = 0.0;
    double low   = 0.0;
    double close = 0.0;
    uint64_t ts  = 0;
};

// =============================================================================
// POSITION
// =============================================================================
struct Position {
    SignalSource source     = SignalSource::NONE;
    double entry            = 0.0;
    double stop             = 0.0;
    double target           = 0.0;  // Optional - some strategies use time exit
    double size             = 0.0;
    uint64_t entry_ts       = 0;
    int bars_held           = 0;    // For time-based exits
    int hold_target         = 0;    // How many bars to hold
};

// =============================================================================
// TRADE RECORD
// =============================================================================
struct TradeRecord {
    char symbol[16]         = "XAUUSD";
    SignalSource source     = SignalSource::NONE;
    double entry_price      = 0.0;
    double exit_price       = 0.0;
    double size             = 0.0;
    double pnl_dollars      = 0.0;
    double pnl_points       = 0.0;
    char exit_reason[32]    = {0};
    uint64_t entry_ts       = 0;
    uint64_t exit_ts        = 0;
};

// =============================================================================
// XAU VALIDATED ENGINE
// =============================================================================
class XauValidatedEngine {
public:
    using OrderCallback = std::function<void(const char* symbol, bool is_buy, double qty)>;
    using TradeCallback = std::function<void(const TradeRecord& trade)>;

    XauValidatedEngine() {
        // Initialize bar buffers
        bars_4h_.resize(25);  // Need 20 + buffer
        bars_1h_.resize(10);  // Need 5 + buffer
    }

    // =========================================================================
    // CONFIGURATION
    // =========================================================================
    void setOrderCallback(OrderCallback cb) { order_callback_ = std::move(cb); }
    void setTradeCallback(TradeCallback cb) { trade_callback_ = std::move(cb); }
    void setEquity(double equity) { equity_ = equity; }
    void setRiskPercent(double pct) { risk_pct_ = pct; }
    void setMaxSpread(double spread) { max_spread_ = spread; }
    
    // Enable/disable individual strategies
    void enableFadeLow(bool e)     { enable_fade_low_ = e; }
    void enableRangeBreak(bool e)  { enable_range_break_ = e; }
    void enableVolBreak(bool e)    { enable_vol_break_ = e; }
    void enableMicroAlpha(bool e)  { enable_micro_alpha_ = e; }
    
    // Master kill switch
    void kill() { killed_ = true; forceExit("KILLED"); }
    void unkill() { killed_ = false; }
    bool isKilled() const { return killed_; }

    // =========================================================================
    // DATA FEEDS
    // =========================================================================
    
    // 4H bar feed - for FADE_LOW and RANGE_BREAK
    void on4HBar(double o, double h, double l, double c, uint64_t ts = 0) {
        if (killed_) return;
        
        Bar bar{o, h, l, c, ts ? ts : nowMs()};
        bars_4h_.push_back(bar);
        if (bars_4h_.size() > 25) bars_4h_.pop_front();
        
        last_price_ = c;
        
        // Manage existing position (increment bars held)
        if (hasPosition() && (pos_.source == SignalSource::FADE_LOW || 
                              pos_.source == SignalSource::RANGE_BREAK)) {
            pos_.bars_held++;
            managePosition4H();
        }
        
        // Try new entries if flat
        if (!hasPosition()) {
            tryFadeLow();
            if (!hasPosition()) tryRangeBreak();
        }
    }
    
    // 1H bar feed - for VOL_BREAK
    void on1HBar(double o, double h, double l, double c, uint64_t ts = 0) {
        if (killed_) return;
        
        Bar bar{o, h, l, c, ts ? ts : nowMs()};
        bars_1h_.push_back(bar);
        if (bars_1h_.size() > 10) bars_1h_.pop_front();
        
        last_price_ = c;
        
        // Manage existing position
        if (hasPosition() && pos_.source == SignalSource::VOL_BREAK) {
            pos_.bars_held++;
            managePosition1H();
        }
        
        // Try new entry if flat
        if (!hasPosition()) {
            tryVolBreak();
        }
    }
    
    // Tick feed - for MICRO_ALPHA
    void onTick(double bid, double ask, uint64_t ts = 0) {
        if (killed_) return;
        
        uint64_t now = ts ? ts : nowMs();
        double mid = (bid + ask) * 0.5;
        double spread = ask - bid;
        
        // Update tick buffer for impulse detection
        tick_times_.push_back(now);
        tick_mids_.push_back(mid);
        tick_spreads_.push_back(spread);
        
        // Keep last 500 ticks
        while (tick_times_.size() > 500) {
            tick_times_.pop_front();
            tick_mids_.pop_front();
            tick_spreads_.pop_front();
        }
        
        // Update median spread (EMA)
        median_spread_ = 0.98 * median_spread_ + 0.02 * spread;
        
        last_price_ = mid;
        last_spread_ = spread;
        
        // Manage micro alpha position
        if (hasPosition() && pos_.source == SignalSource::MICRO_ALPHA) {
            manageMicroAlpha(mid, now);
        }
        
        // Try micro alpha entry if flat and spread ok
        if (!hasPosition() && spread <= max_spread_) {
            tryMicroAlpha(mid, now);
        }
    }

    // =========================================================================
    // STATE QUERIES
    // =========================================================================
    bool hasPosition() const { return pos_.source != SignalSource::NONE; }
    const Position& getPosition() const { return pos_; }
    SignalSource getCurrentSource() const { return pos_.source; }
    int getTotalTrades() const { return total_trades_; }
    double getTotalPnL() const { return total_pnl_; }
    
    void forceExit(const char* reason = "FORCED") {
        if (hasPosition()) {
            exitPosition(reason);
        }
    }
    
    void reset() {
        pos_ = Position{};
        impulse_detected_ = false;
        total_trades_ = 0;
        total_pnl_ = 0;
        killed_ = false;
    }

private:
    // =========================================================================
    // FIXED PARAMETERS - VALIDATED, DO NOT CHANGE
    // =========================================================================
    
    // FADE_LOW (4H)
    static constexpr int FADE_LOW_LOOKBACK = 20;
    static constexpr int FADE_LOW_HOLD     = 3;   // 12 hours
    
    // RANGE_BREAK (4H)
    static constexpr int RANGE_BREAK_LOOKBACK = 10;
    static constexpr int RANGE_BREAK_HOLD     = 3;   // 12 hours
    
    // VOL_BREAK (1H)
    static constexpr int VOL_BREAK_LOOKBACK   = 5;
    static constexpr double VOL_BREAK_MULT    = 2.0;
    static constexpr int VOL_BREAK_HOLD       = 5;   // 5 hours
    
    // MICRO_ALPHA (tick) - FIXED from validation
    static constexpr double IMPULSE_POINTS    = 6.916;
    static constexpr uint64_t IMPULSE_MS      = 5000;
    static constexpr uint64_t STALL_MS        = 2000;
    static constexpr double STOP_BUFFER       = 1.383;
    static constexpr double TARGET_FRAC       = 0.40;
    static constexpr uint64_t MAX_HOLD_MS     = 20000;
    static constexpr double SPREAD_MULT       = 1.62;
    static constexpr double ENTRY_ZONE        = 0.15;

    // =========================================================================
    // STATE
    // =========================================================================
    Position pos_;
    
    // Bar buffers
    std::deque<Bar> bars_4h_;
    std::deque<Bar> bars_1h_;
    
    // Tick buffers (for micro alpha)
    std::deque<uint64_t> tick_times_;
    std::deque<double> tick_mids_;
    std::deque<double> tick_spreads_;
    
    // Micro alpha state
    bool impulse_detected_ = false;
    double impulse_high_   = 0.0;
    double impulse_low_    = 0.0;
    uint64_t impulse_ts_   = 0;
    double median_spread_  = 0.5;
    
    // Current market
    double last_price_     = 0.0;
    double last_spread_    = 0.0;
    
    // Config
    double equity_         = 100000.0;
    double risk_pct_       = 0.001;   // 0.1% risk per trade
    double max_spread_     = 1.0;     // Max spread in points
    
    // Strategy enables
    bool enable_fade_low_    = true;
    bool enable_range_break_ = true;
    bool enable_vol_break_   = true;
    bool enable_micro_alpha_ = true;
    bool killed_             = false;
    
    // Stats
    int total_trades_      = 0;
    double total_pnl_      = 0.0;
    
    // Callbacks
    OrderCallback order_callback_;
    TradeCallback trade_callback_;

    // =========================================================================
    // FADE_LOW STRATEGY (4H)
    // =========================================================================
    void tryFadeLow() {
        if (!enable_fade_low_) return;
        if (bars_4h_.size() < FADE_LOW_LOOKBACK + 1) return;
        
        // Calculate 20-bar low (excluding current bar)
        double lowest = 1e9;
        for (size_t i = bars_4h_.size() - FADE_LOW_LOOKBACK - 1; 
             i < bars_4h_.size() - 1; i++) {
            if (bars_4h_[i].low < lowest) lowest = bars_4h_[i].low;
        }
        
        // Previous bar close
        double prev_close = bars_4h_[bars_4h_.size() - 2].close;
        
        // SIGNAL: Close below 20-bar low = failed breakdown = BUY
        if (prev_close < lowest) {
            enterLong(SignalSource::FADE_LOW, FADE_LOW_HOLD, 0.0, 0.0);
            printf("[XAU-VALIDATED] FADE_LOW entry @ %.2f (20-bar low: %.2f)\n", 
                   last_price_, lowest);
        }
    }
    
    void managePosition4H() {
        // Time-based exit
        if (pos_.bars_held >= pos_.hold_target) {
            exitPosition("TIME");
        }
    }

    // =========================================================================
    // RANGE_BREAK STRATEGY (4H)
    // =========================================================================
    void tryRangeBreak() {
        if (!enable_range_break_) return;
        if (bars_4h_.size() < RANGE_BREAK_LOOKBACK + 1) return;
        
        // Calculate 10-bar high (excluding current bar)
        double highest = -1e9;
        for (size_t i = bars_4h_.size() - RANGE_BREAK_LOOKBACK - 1; 
             i < bars_4h_.size() - 1; i++) {
            if (bars_4h_[i].high > highest) highest = bars_4h_[i].high;
        }
        
        double prev_close = bars_4h_[bars_4h_.size() - 2].close;
        
        // SIGNAL: Close above 10-bar high = breakout = BUY
        if (prev_close > highest) {
            enterLong(SignalSource::RANGE_BREAK, RANGE_BREAK_HOLD, 0.0, 0.0);
            printf("[XAU-VALIDATED] RANGE_BREAK entry @ %.2f (10-bar high: %.2f)\n",
                   last_price_, highest);
        }
    }

    // =========================================================================
    // VOL_BREAK STRATEGY (1H)
    // =========================================================================
    void tryVolBreak() {
        if (!enable_vol_break_) return;
        if (bars_1h_.size() < VOL_BREAK_LOOKBACK + 1) return;
        
        // Calculate average range
        double sum_range = 0.0;
        for (size_t i = bars_1h_.size() - VOL_BREAK_LOOKBACK - 1;
             i < bars_1h_.size() - 1; i++) {
            sum_range += (bars_1h_[i].high - bars_1h_[i].low);
        }
        double avg_range = sum_range / VOL_BREAK_LOOKBACK;
        
        // Previous bar
        const Bar& prev = bars_1h_[bars_1h_.size() - 2];
        double bar_range = prev.high - prev.low;
        double bar_body = prev.close - prev.open;
        
        // SIGNAL: Range > 2x average AND bullish = BUY
        if (bar_range >= avg_range * VOL_BREAK_MULT && bar_body > 0) {
            enterLong(SignalSource::VOL_BREAK, VOL_BREAK_HOLD, 0.0, 0.0);
            printf("[XAU-VALIDATED] VOL_BREAK entry @ %.2f (range: %.2f, avg: %.2f)\n",
                   last_price_, bar_range, avg_range);
        }
    }
    
    void managePosition1H() {
        // Time-based exit
        if (pos_.bars_held >= pos_.hold_target) {
            exitPosition("TIME");
        }
    }

    // =========================================================================
    // MICRO_ALPHA STRATEGY (TICK)
    // =========================================================================
    void tryMicroAlpha(double mid, uint64_t now) {
        if (!enable_micro_alpha_) return;
        if (tick_times_.size() < 100) return;
        
        // Find tick at start of impulse window
        size_t start_idx = 0;
        for (size_t i = tick_times_.size() - 1; i > 0; i--) {
            if (now - tick_times_[i] >= IMPULSE_MS) {
                start_idx = i;
                break;
            }
        }
        
        if (start_idx == 0 || start_idx >= tick_times_.size() - 3) return;
        
        double move = mid - tick_mids_[start_idx];
        uint64_t dt = now - tick_times_[start_idx];
        
        // Check impulse conditions
        if (std::abs(move) >= IMPULSE_POINTS && dt <= IMPULSE_MS) {
            bool spread_expanded = last_spread_ >= SPREAD_MULT * median_spread_;
            bool large_move = std::abs(move) >= IMPULSE_POINTS * 1.5;
            
            if (spread_expanded || large_move) {
                impulse_detected_ = true;
                impulse_high_ = std::max(tick_mids_[start_idx], mid);
                impulse_low_ = std::min(tick_mids_[start_idx], mid);
                impulse_ts_ = now;
            }
        }
        
        // Entry logic
        if (impulse_detected_) {
            uint64_t since_stall = now - impulse_ts_;
            
            if (since_stall >= STALL_MS) {
                double range = impulse_high_ - impulse_low_;
                
                // LONG ONLY: enter near bottom
                if (mid <= impulse_low_ + range * ENTRY_ZONE) {
                    double stop = impulse_low_ - STOP_BUFFER;
                    double target = mid + range * TARGET_FRAC;
                    
                    enterLong(SignalSource::MICRO_ALPHA, 0, stop, target);
                    pos_.entry_ts = now;  // Track for time-based exit
                    
                    printf("[XAU-VALIDATED] MICRO_ALPHA entry @ %.2f | stop: %.2f | target: %.2f\n",
                           mid, stop, target);
                    
                    impulse_detected_ = false;
                }
                // Impulse expired (price moved away)
                else if (since_stall > STALL_MS * 3) {
                    impulse_detected_ = false;
                }
            }
        }
    }
    
    void manageMicroAlpha(double mid, uint64_t now) {
        uint64_t held_ms = now - pos_.entry_ts;
        
        // Stop hit
        if (mid <= pos_.stop) {
            exitPosition("STOP");
            return;
        }
        
        // Target hit
        if (pos_.target > 0 && mid >= pos_.target) {
            exitPosition("TARGET");
            return;
        }
        
        // Time exit
        if (held_ms >= MAX_HOLD_MS) {
            exitPosition("TIME");
            return;
        }
    }

    // =========================================================================
    // ENTRY/EXIT
    // =========================================================================
    void enterLong(SignalSource source, int hold_bars, double stop, double target) {
        if (hasPosition()) return;  // Already in position
        
        double size = calculateSize();
        
        pos_.source = source;
        pos_.entry = last_price_;
        pos_.stop = stop;
        pos_.target = target;
        pos_.size = size;
        pos_.entry_ts = nowMs();
        pos_.bars_held = 0;
        pos_.hold_target = hold_bars;
        
        if (order_callback_) {
            order_callback_("XAUUSD", true, size);
        }
    }
    
    void exitPosition(const char* reason) {
        if (!hasPosition()) return;
        
        double pnl_points = last_price_ - pos_.entry;
        double pnl_dollars = pnl_points * pos_.size * 100.0;  // $100/pt/lot
        
        // Record trade
        TradeRecord record;
        std::strncpy(record.symbol, "XAUUSD", sizeof(record.symbol) - 1);
        record.source = pos_.source;
        record.entry_price = pos_.entry;
        record.exit_price = last_price_;
        record.size = pos_.size;
        record.pnl_dollars = pnl_dollars;
        record.pnl_points = pnl_points;
        std::strncpy(record.exit_reason, reason, sizeof(record.exit_reason) - 1);
        record.entry_ts = pos_.entry_ts;
        record.exit_ts = nowMs();
        
        if (trade_callback_) {
            trade_callback_(record);
        }
        
        if (order_callback_) {
            order_callback_("XAUUSD", false, pos_.size);  // SELL to close
        }
        
        printf("[XAU-VALIDATED] EXIT %s @ %.2f reason=%s pnl=%.2f pts ($%.2f)\n",
               signalSourceStr(pos_.source), last_price_, reason, 
               pnl_points, pnl_dollars);
        
        total_trades_++;
        total_pnl_ += pnl_dollars;
        
        // Reset position
        pos_ = Position{};
        impulse_detected_ = false;
    }
    
    double calculateSize() const {
        // Risk-based sizing: risk_pct * equity / assumed_stop
        // Use conservative 15 point stop estimate for bar strategies
        double assumed_stop = 15.0;  // $15 stop
        double risk_dollars = equity_ * risk_pct_;
        double size = risk_dollars / (assumed_stop * 100.0);  // $100/pt/lot
        return std::max(0.01, std::min(1.0, size));  // Clamp 0.01 - 1.0 lots
    }
    
    uint64_t nowMs() const {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }
};

} // namespace xau_validated
