// =============================================================================
// IndexImpulseEngine.hpp - v4.10.2 - E2-PRIMARY INDEX ENGINE
// =============================================================================
// CRITICAL ARCHITECTURE CHANGE (v4.10.2):
//   E2 IS THE PRIMARY EXECUTOR
//   E1 IS A FILTER ONLY - CANNOT OPEN TRADES INDEPENDENTLY
//
// WHY E2 DOMINATES:
//   - E2 (VWAP pullback) is the proven alpha
//   - E1 (compression/impulse) improves E2 quality but doesn't trade alone
//   - This matches backtest results: E2 = core earner
//
// ENGINE 2 — PRIMARY EXECUTOR
//   Entry: VWAP pullback with structure confirmation
//   Controls: direction, timing, position opening
//   Exit: Partial + Runner + Stall Kill
//
// ENGINE 1 — FILTER ONLY (cannot open trades)
//   Role: Compression quality gate for E2
//   If compression detected: E2 entries get higher confidence
//   If no compression: E2 can still trade but with tighter criteria
//
// EXIT ENGINE (v4.10.2 - THIS IS WHERE MONEY IS MADE):
//   1. PARTIAL PROFIT: 60% at structural level
//   2. STALL KILL: Exit if no progress after N bars
//   3. RUNNER PROTECTION: Stop to BE after partial
//
// SYMBOLS: NAS100, US30 only (v4.10.2 lock)
// RISK: Fixed 0.5% NAS100, 0.4% US30 (no scaling)
//
// OWNERSHIP: Jo
// LAST VERIFIED: 2025-01-06
// =============================================================================
#pragma once

#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <array>
#include <unordered_map>
#include <string>
#include <chrono>

#include "quality/MarketQualityCuts.hpp"

namespace Chimera {

// =============================================================================
// ALLOWED SYMBOLS (v4.10.2 HARD LOCK)
// =============================================================================
inline bool isAllowedIndexSymbol(const char* symbol) {
    return (strstr(symbol, "NAS100") != nullptr || 
            strstr(symbol, "US30") != nullptr);
}

// =============================================================================
// ENGINE STATE MACHINE (v4.10.2 - E2 PRIMARY)
// =============================================================================
enum class IndexEngineState : uint8_t {
    IDLE = 0,              // Outside session
    WATCHING = 1,          // In session, building context
    E2_ELIGIBLE = 2,       // E2 can look for entries
    E2_POSITION = 3,       // E2 position open
    DONE = 4               // Session complete
};

inline const char* indexEngineStateStr(IndexEngineState s) {
    switch (s) {
        case IndexEngineState::IDLE:        return "IDLE";
        case IndexEngineState::WATCHING:    return "WATCHING";
        case IndexEngineState::E2_ELIGIBLE: return "E2_ELIGIBLE";
        case IndexEngineState::E2_POSITION: return "E2_POSITION";
        case IndexEngineState::DONE:        return "DONE";
        default:                            return "UNKNOWN";
    }
}

// =============================================================================
// EXIT STAGE (v4.10.2 - PARTIAL + RUNNER)
// =============================================================================
enum class ExitStage : uint8_t {
    INITIAL = 0,           // Full position, initial stop
    PARTIAL_TAKEN = 1,     // Partial profit taken, runner active
    RUNNER_PROTECTED = 2,  // Stop at BE or better
    TRAILING = 3           // Aggressive trail on runner
};

inline const char* exitStageStr(ExitStage s) {
    switch (s) {
        case ExitStage::INITIAL:          return "INITIAL";
        case ExitStage::PARTIAL_TAKEN:    return "PARTIAL_TAKEN";
        case ExitStage::RUNNER_PROTECTED: return "RUNNER_PROTECTED";
        case ExitStage::TRAILING:         return "TRAILING";
        default:                          return "UNKNOWN";
    }
}

// =============================================================================
// POSITION TRACKING (v4.10.2 - WITH EXIT ENGINE STATE)
// =============================================================================
struct IndexPosition {
    bool active = false;
    int8_t side = 0;             // +1 long, -1 short
    double entry_price = 0.0;
    double stop_loss = 0.0;
    double initial_risk = 0.0;   // 1R in price terms
    double initial_size = 0.0;   // Original size
    double current_size = 0.0;   // After partials
    uint64_t entry_ts = 0;
    uint32_t bars_in_trade = 0;  // For stall kill
    
    // Exit engine state
    ExitStage exit_stage = ExitStage::INITIAL;
    double partial_target = 0.0;  // Structural level for partial
    double runner_stop = 0.0;     // Stop for runner after partial
    double highest_r = 0.0;       // Track best R achieved
    
    // Swing tracking
    double last_swing_high = 0.0;
    double last_swing_low = 0.0;
    
    void reset() {
        active = false;
        side = 0;
        entry_price = 0.0;
        stop_loss = 0.0;
        initial_risk = 0.0;
        initial_size = 0.0;
        current_size = 0.0;
        entry_ts = 0;
        bars_in_trade = 0;
        exit_stage = ExitStage::INITIAL;
        partial_target = 0.0;
        runner_stop = 0.0;
        highest_r = 0.0;
        last_swing_high = 0.0;
        last_swing_low = 0.0;
    }
    
    [[nodiscard]] double currentR(double current_price) const {
        if (!active || initial_risk <= 0.0) return 0.0;
        double pnl = (side > 0) 
            ? (current_price - entry_price) 
            : (entry_price - current_price);
        return pnl / initial_risk;
    }
};

// =============================================================================
// PER-SYMBOL STATE (v4.10.2)
// =============================================================================
struct IndexSymbolState {
    IndexEngineState state = IndexEngineState::IDLE;
    
    // Rolling metrics
    double atr_20 = 0.0;
    double vwap = 0.0;
    double vwap_pv_sum = 0.0;
    double vwap_vol_sum = 0.0;
    
    // E1 FILTER: Compression quality (NOT a trading signal)
    bool compression_active = false;
    double compression_quality = 0.0;  // 0-1 score
    
    // Bar data
    static constexpr size_t BAR_HISTORY = 30;
    std::array<double, BAR_HISTORY> bar_highs{};
    std::array<double, BAR_HISTORY> bar_lows{};
    std::array<double, BAR_HISTORY> bar_closes{};
    std::array<double, BAR_HISTORY> bar_ranges{};
    size_t bar_idx = 0;
    size_t bar_count = 0;
    
    // ATR calculation
    std::array<double, 20> tr_history{};
    size_t tr_idx = 0;
    size_t tr_count = 0;
    double prev_close = 0.0;
    
    // Current bar
    double bar_open = 0.0;
    double bar_high = 0.0;
    double bar_low = 0.0;
    double bar_close = 0.0;
    uint32_t bar_tick_count = 0;
    
    // Structure levels for E2
    double or_high = 0.0;        // Opening range high
    double or_low = 0.0;         // Opening range low
    double or_mid = 0.0;         // Opening range midpoint
    bool or_recorded = false;
    
    // Position (E2 only)
    IndexPosition position;
    
    // Daily tracking
    int trades_today = 0;
    
    void resetDaily() {
        state = IndexEngineState::IDLE;
        position.reset();
        trades_today = 0;
        compression_active = false;
        compression_quality = 0.0;
        vwap_pv_sum = 0.0;
        vwap_vol_sum = 0.0;
        vwap = 0.0;
        or_high = 0.0;
        or_low = 0.0;
        or_mid = 0.0;
        or_recorded = false;
    }
    
    void updateBar(double price, double volume) {
        if (bar_tick_count == 0) {
            bar_open = price;
            bar_high = price;
            bar_low = price;
        }
        
        bar_high = std::max(bar_high, price);
        bar_low = std::min(bar_low, price);
        bar_close = price;
        bar_tick_count++;
        
        // Update VWAP
        vwap_pv_sum += price * volume;
        vwap_vol_sum += volume;
        if (vwap_vol_sum > 0.0) {
            vwap = vwap_pv_sum / vwap_vol_sum;
        }
    }
    
    void closeBar() {
        if (bar_tick_count == 0) return;
        
        // Calculate True Range
        double tr = bar_high - bar_low;
        if (prev_close > 0.0) {
            tr = std::max(tr, std::abs(bar_high - prev_close));
            tr = std::max(tr, std::abs(bar_low - prev_close));
        }
        
        // Update TR history
        tr_history[tr_idx] = tr;
        tr_idx = (tr_idx + 1) % 20;
        if (tr_count < 20) tr_count++;
        
        // Calculate ATR(20)
        if (tr_count > 0) {
            double sum = 0.0;
            for (size_t i = 0; i < tr_count; i++) sum += tr_history[i];
            atr_20 = sum / tr_count;
        }
        
        // Update bar history
        bar_highs[bar_idx] = bar_high;
        bar_lows[bar_idx] = bar_low;
        bar_closes[bar_idx] = bar_close;
        bar_ranges[bar_idx] = bar_high - bar_low;
        bar_idx = (bar_idx + 1) % BAR_HISTORY;
        if (bar_count < BAR_HISTORY) bar_count++;
        
        // Update swing points
        updateSwings();
        
        // Increment bars in trade
        if (position.active) {
            position.bars_in_trade++;
        }
        
        // Reset for next bar
        prev_close = bar_close;
        bar_tick_count = 0;
    }
    
    void updateSwings() {
        if (bar_count < 3) return;
        
        size_t idx_1 = (bar_idx + BAR_HISTORY - 1) % BAR_HISTORY;
        size_t idx_2 = (bar_idx + BAR_HISTORY - 2) % BAR_HISTORY;
        size_t idx_3 = (bar_idx + BAR_HISTORY - 3) % BAR_HISTORY;
        
        if (bar_highs[idx_2] > bar_highs[idx_1] && bar_highs[idx_2] > bar_highs[idx_3]) {
            position.last_swing_high = bar_highs[idx_2];
        }
        
        if (bar_lows[idx_2] < bar_lows[idx_1] && bar_lows[idx_2] < bar_lows[idx_3]) {
            position.last_swing_low = bar_lows[idx_2];
        }
    }
    
    // E1 FILTER: Calculate compression quality (0-1 score)
    void updateCompressionFilter() {
        if (bar_count < 20 || atr_20 <= 0.0) {
            compression_active = false;
            compression_quality = 0.0;
            return;
        }
        
        // Calculate recent range vs ATR
        double max_h = 0.0, min_l = 1e12;
        size_t lookback = std::min(bar_count, (size_t)10);
        for (size_t i = 0; i < lookback; i++) {
            size_t idx = (bar_idx + BAR_HISTORY - 1 - i) % BAR_HISTORY;
            max_h = std::max(max_h, bar_highs[idx]);
            min_l = std::min(min_l, bar_lows[idx]);
        }
        double recent_range = max_h - min_l;
        
        // Compression = recent range < ATR * 0.6
        double ratio = recent_range / (atr_20 * 0.6);
        compression_active = (ratio < 1.0);
        compression_quality = compression_active ? (1.0 - ratio) : 0.0;
    }
};

// =============================================================================
// ENGINE CONFIG (v4.10.2 - FIXED RISK)
// =============================================================================
struct IndexEngineConfig {
    // Session (NY time as UTC)
    int session_start_hour = 13;    // 09:30 NY = 13:30 UTC
    int session_start_min = 30;
    int session_end_hour = 19;      // 15:00 NY = 19:00 UTC
    int session_end_min = 0;
    
    // Opening range window (first 30 mins)
    int or_end_min = 30;            // OR ends at 10:00 NY
    
    // E2 Entry: VWAP pullback threshold
    double vwap_threshold = 0.2;    // Within 0.2 * ATR of VWAP
    
    // FIXED RISK (v4.10.2 - NO SCALING)
    double nas100_risk = 0.005;     // 0.5% fixed
    double us30_risk = 0.004;       // 0.4% fixed
    
    // EXIT ENGINE CONFIG
    double partial_pct = 0.60;      // Take 60% at partial
    double partial_r = 1.0;         // Partial at +1R
    int stall_bars = 6;             // Stall kill after 6 bars
    double runner_be_buffer = 0.1;  // Runner stop = entry + 0.1R
    double runner_trail_r = 2.0;    // Start trailing at +2R
    
    // Max trades per symbol per day
    int max_trades_per_day = 2;
};

// =============================================================================
// ENGINE OUTPUT
// =============================================================================
struct EngineOutput {
    bool should_trade = false;
    int8_t direction = 0;
    double size = 0.0;
    double stop_loss = 0.0;
    const char* engine = "";
    const char* reason = "";
    bool is_exit = false;
    bool is_partial = false;
    double realized_pnl = 0.0;
};

// =============================================================================
// INDEX IMPULSE ENGINE (v4.10.2 - E2 PRIMARY)
// =============================================================================
class IndexImpulseEngine {
public:
    IndexImpulseEngine() = default;
    
    void setConfig(const IndexEngineConfig& cfg) { config_ = cfg; }
    const IndexEngineConfig& config() const { return config_; }
    
    // =========================================================================
    // TICK HANDLER
    // =========================================================================
    EngineOutput onTick(
        const char* symbol,
        double bid,
        double ask,
        double volume,
        uint64_t now_ns,
        double equity
    ) {
        EngineOutput out;
        out.reason = "IDLE";
        
        // v4.10.2: Hard symbol lock
        if (!isAllowedIndexSymbol(symbol)) {
            out.reason = "SYMBOL_NOT_ALLOWED";
            return out;
        }
        
        double mid = (bid + ask) / 2.0;
        
        // Get or create symbol state
        auto& ss = symbol_states_[symbol];
        
        // Update bar data
        ss.updateBar(mid, volume);
        
        // Update E1 filter (compression quality)
        ss.updateCompressionFilter();
        
        // Check session
        if (!isInSession(now_ns)) {
            if (ss.state != IndexEngineState::IDLE && 
                ss.state != IndexEngineState::DONE) {
                ss.state = IndexEngineState::IDLE;
            }
            out.reason = "OUT_OF_SESSION";
            return out;
        }
        
        // Record opening range (first 30 mins)
        if (!ss.or_recorded && isInORWindow(now_ns)) {
            if (ss.or_high == 0.0) {
                ss.or_high = mid;
                ss.or_low = mid;
            }
            ss.or_high = std::max(ss.or_high, mid);
            ss.or_low = std::min(ss.or_low, mid);
        } else if (!ss.or_recorded && !isInORWindow(now_ns)) {
            ss.or_mid = (ss.or_high + ss.or_low) / 2.0;
            ss.or_recorded = true;
            printf("[INDEX-E2] %s OR recorded: H=%.2f L=%.2f M=%.2f\n",
                   symbol, ss.or_high, ss.or_low, ss.or_mid);
        }
        
        // =====================================================================
        // MANAGE EXISTING POSITION (EXIT ENGINE)
        // =====================================================================
        if (ss.position.active) {
            out = managePosition(symbol, ss, mid, now_ns);
            if (out.should_trade) return out;
        }
        
        // =====================================================================
        // STATE MACHINE (E2 PRIMARY)
        // =====================================================================
        switch (ss.state) {
            case IndexEngineState::IDLE:
            case IndexEngineState::WATCHING:
                ss.state = IndexEngineState::WATCHING;
                out.reason = "WATCHING";
                
                // Transition to E2_ELIGIBLE after OR recorded
                if (ss.or_recorded && ss.atr_20 > 0.0) {
                    ss.state = IndexEngineState::E2_ELIGIBLE;
                    printf("[INDEX-E2] %s E2 eligible: ATR=%.2f compression=%s (%.2f)\n",
                           symbol, ss.atr_20, 
                           ss.compression_active ? "YES" : "NO",
                           ss.compression_quality);
                }
                break;
                
            case IndexEngineState::E2_ELIGIBLE:
                out.reason = "E2_ELIGIBLE";
                
                // Check for E2 entry
                if (!ss.position.active && ss.trades_today < config_.max_trades_per_day) {
                    if (detectE2Entry(ss, mid)) {
                        out = executeE2Entry(symbol, ss, mid, equity);
                    }
                }
                break;
                
            case IndexEngineState::E2_POSITION:
                out.reason = "E2_POSITION_ACTIVE";
                break;
                
            case IndexEngineState::DONE:
                out.reason = "SESSION_DONE";
                break;
                
            default:
                break;
        }
        
        return out;
    }
    
    // =========================================================================
    // BAR CLOSE HANDLER
    // =========================================================================
    void onBarClose(const char* symbol) {
        auto it = symbol_states_.find(symbol);
        if (it != symbol_states_.end()) {
            it->second.closeBar();
        }
    }
    
    // =========================================================================
    // DAILY RESET
    // =========================================================================
    void resetDaily() {
        for (auto& [sym, ss] : symbol_states_) {
            ss.resetDaily();
        }
        printf("[INDEX-E2] Daily reset complete\n");
    }
    
    // =========================================================================
    // STATUS
    // =========================================================================
    void printStatus() const {
        printf("[INDEX-E2] Engine Status (v4.10.2 - E2 PRIMARY):\n");
        printf("  Allowed symbols: NAS100, US30 only\n");
        printf("  Risk: NAS100=%.1f%%, US30=%.1f%% (FIXED)\n",
               config_.nas100_risk * 100.0, config_.us30_risk * 100.0);
        
        for (const auto& [sym, ss] : symbol_states_) {
            printf("  %s: state=%s trades=%d ATR=%.2f compression=%s\n",
                   sym.c_str(), indexEngineStateStr(ss.state),
                   ss.trades_today, ss.atr_20,
                   ss.compression_active ? "YES" : "NO");
            if (ss.position.active) {
                printf("    Position: side=%d entry=%.2f SL=%.2f R=%.2f exit=%s\n",
                       ss.position.side, ss.position.entry_price,
                       ss.position.stop_loss,
                       ss.position.currentR(ss.bar_close),
                       exitStageStr(ss.position.exit_stage));
            }
        }
    }

private:
    IndexEngineConfig config_;
    std::unordered_map<std::string, IndexSymbolState> symbol_states_;
    
    // =========================================================================
    // SESSION CHECK
    // =========================================================================
    [[nodiscard]] bool isInSession([[maybe_unused]] uint64_t now_ns) const {
        auto now_tp = std::chrono::system_clock::now();
        auto now_time = std::chrono::system_clock::to_time_t(now_tp);
        std::tm* utc_tm = std::gmtime(&now_time);
        
        int hour = utc_tm->tm_hour;
        int min = utc_tm->tm_min;
        int time_mins = hour * 60 + min;
        
        int session_start = config_.session_start_hour * 60 + config_.session_start_min;
        int session_end = config_.session_end_hour * 60 + config_.session_end_min;
        
        return time_mins >= session_start && time_mins <= session_end;
    }
    
    [[nodiscard]] bool isInORWindow([[maybe_unused]] uint64_t now_ns) const {
        auto now_tp = std::chrono::system_clock::now();
        auto now_time = std::chrono::system_clock::to_time_t(now_tp);
        std::tm* utc_tm = std::gmtime(&now_time);
        
        int hour = utc_tm->tm_hour;
        int min = utc_tm->tm_min;
        int time_mins = hour * 60 + min;
        
        int session_start = config_.session_start_hour * 60 + config_.session_start_min;
        int or_end = session_start + config_.or_end_min;
        
        return time_mins >= session_start && time_mins < or_end;
    }
    
    // =========================================================================
    // E2 ENTRY DETECTION (PRIMARY)
    // =========================================================================
    [[nodiscard]] bool detectE2Entry(const IndexSymbolState& ss, double mid) const {
        if (ss.atr_20 <= 0.0 || ss.vwap <= 0.0) return false;
        if (!ss.or_recorded) return false;
        
        // VWAP pullback: price within threshold of VWAP
        double dist_to_vwap = std::abs(mid - ss.vwap);
        if (dist_to_vwap > ss.atr_20 * config_.vwap_threshold) {
            return false;
        }
        
        // Determine direction from OR structure
        // Long: price pulled back to VWAP from above OR high
        // Short: price pulled back to VWAP from below OR low
        
        bool long_setup = (mid > ss.or_mid && mid <= ss.vwap + ss.atr_20 * 0.1);
        bool short_setup = (mid < ss.or_mid && mid >= ss.vwap - ss.atr_20 * 0.1);
        
        if (!long_setup && !short_setup) return false;
        
        // E1 FILTER: Compression improves quality but doesn't block
        // If compression active, we have higher confidence
        // If no compression, still trade but criteria already met
        
        printf("[INDEX-E2] Entry signal: %s setup=%s compression=%s(%.2f)\n",
               long_setup ? "LONG" : "SHORT",
               long_setup ? "LONG" : "SHORT",
               ss.compression_active ? "YES" : "NO",
               ss.compression_quality);
        
        return true;
    }
    
    // =========================================================================
    // E2 ENTRY EXECUTION
    // =========================================================================
    EngineOutput executeE2Entry(
        const char* symbol,
        IndexSymbolState& ss,
        double mid,
        double equity
    ) {
        EngineOutput out;
        
        // Determine direction
        int8_t direction = (mid > ss.or_mid) ? 1 : -1;
        
        // Calculate stop
        double sl;
        if (direction > 0) {
            sl = ss.or_low;  // Stop below OR low for longs
        } else {
            sl = ss.or_high;  // Stop above OR high for shorts
        }
        
        double risk_points = std::abs(mid - sl);
        if (risk_points <= 0.0) {
            out.reason = "INVALID_STOP";
            return out;
        }
        
        // FIXED RISK (v4.10.2)
        double risk_pct = (strstr(symbol, "NAS100") != nullptr) 
            ? config_.nas100_risk 
            : config_.us30_risk;
        double risk_dollars = equity * risk_pct;
        double size = risk_dollars / risk_points;
        
        // Calculate partial target (structural level)
        double partial_target;
        if (direction > 0) {
            partial_target = ss.or_high;  // Partial at OR high for longs
        } else {
            partial_target = ss.or_low;   // Partial at OR low for shorts
        }
        
        // Setup position
        ss.position.active = true;
        ss.position.side = direction;
        ss.position.entry_price = mid;
        ss.position.stop_loss = sl;
        ss.position.initial_risk = risk_points;
        ss.position.initial_size = size;
        ss.position.current_size = size;
        ss.position.entry_ts = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        ss.position.bars_in_trade = 0;
        ss.position.exit_stage = ExitStage::INITIAL;
        ss.position.partial_target = partial_target;
        ss.position.runner_stop = mid + (direction > 0 ? 1 : -1) * risk_points * config_.runner_be_buffer;
        ss.position.highest_r = 0.0;
        
        ss.trades_today++;
        ss.state = IndexEngineState::E2_POSITION;
        
        printf("[INDEX-E2] %s ENTRY %s @ %.2f SL=%.2f risk=%.1f%% size=%.4f partial_target=%.2f\n",
               symbol, direction > 0 ? "LONG" : "SHORT",
               mid, sl, risk_pct * 100.0, size, partial_target);
        
        out.should_trade = true;
        out.direction = direction;
        out.size = size;
        out.stop_loss = sl;
        out.engine = "E2";
        out.reason = "VWAP_PULLBACK";
        
        return out;
    }
    
    // =========================================================================
    // POSITION MANAGEMENT (EXIT ENGINE)
    // =========================================================================
    EngineOutput managePosition(
        const char* symbol,
        IndexSymbolState& ss,
        double mid,
        [[maybe_unused]] uint64_t now_ns
    ) {
        EngineOutput out;
        auto& pos = ss.position;
        
        double current_r = pos.currentR(mid);
        pos.highest_r = std::max(pos.highest_r, current_r);
        
        // =====================================================================
        // CHECK 1: HARD STOP (always first)
        // =====================================================================
        bool stopped = false;
        if (pos.side > 0 && mid <= pos.stop_loss) stopped = true;
        if (pos.side < 0 && mid >= pos.stop_loss) stopped = true;
        
        if (stopped) {
            double pnl_points = (pos.side > 0) 
                ? (mid - pos.entry_price) 
                : (pos.entry_price - mid);
            
            printf("[INDEX-E2] %s EXIT SL @ %.2f PnL=%.2fR exit_stage=%s\n",
                   symbol, mid, current_r, exitStageStr(pos.exit_stage));
            
            out.should_trade = true;
            out.direction = -pos.side;
            out.size = pos.current_size;
            out.engine = "E2";
            out.reason = "STOP_LOSS";
            out.is_exit = true;
            out.realized_pnl = pnl_points * pos.current_size;
            
            pos.reset();
            ss.state = IndexEngineState::E2_ELIGIBLE;
            
            return out;
        }
        
        // =====================================================================
        // CHECK 2: STALL KILL (no progress after N bars)
        // =====================================================================
        if (pos.exit_stage == ExitStage::INITIAL && 
            pos.bars_in_trade >= (uint32_t)config_.stall_bars) {
            
            if (current_r <= 0.0) {
                // No progress after stall_bars - kill it
                printf("[INDEX-E2] %s EXIT STALL @ %.2f bars=%u PnL=%.2fR reason=NO_PROGRESS\n",
                       symbol, mid, pos.bars_in_trade, current_r);
                
                out.should_trade = true;
                out.direction = -pos.side;
                out.size = pos.current_size;
                out.engine = "E2";
                out.reason = "STALL_KILL";
                out.is_exit = true;
                out.realized_pnl = current_r * pos.initial_risk * pos.current_size;
                
                pos.reset();
                ss.state = IndexEngineState::E2_ELIGIBLE;
                
                return out;
            }
        }
        
        // =====================================================================
        // CHECK 3: PARTIAL PROFIT (at structural level)
        // =====================================================================
        if (pos.exit_stage == ExitStage::INITIAL) {
            bool partial_hit = false;
            if (pos.side > 0 && mid >= pos.partial_target) partial_hit = true;
            if (pos.side < 0 && mid <= pos.partial_target) partial_hit = true;
            
            if (partial_hit && current_r >= config_.partial_r) {
                double partial_size = pos.initial_size * config_.partial_pct;
                
                printf("[INDEX-E2] %s PARTIAL @ %.2f size=%.4f PnL=%.2fR\n",
                       symbol, mid, partial_size, current_r);
                
                // Move stop to BE (runner protection)
                pos.stop_loss = pos.runner_stop;
                pos.current_size = pos.initial_size - partial_size;
                pos.exit_stage = ExitStage::PARTIAL_TAKEN;
                
                printf("[INDEX-E2] %s Runner protected: stop moved to %.2f (BE+buffer)\n",
                       symbol, pos.stop_loss);
                
                out.should_trade = true;
                out.direction = -pos.side;
                out.size = partial_size;
                out.engine = "E2";
                out.reason = "PARTIAL_PROFIT";
                out.is_exit = true;
                out.is_partial = true;
                out.realized_pnl = current_r * pos.initial_risk * partial_size;
                
                return out;
            }
        }
        
        // =====================================================================
        // CHECK 4: RUNNER TRAIL (after partial)
        // =====================================================================
        if (pos.exit_stage == ExitStage::PARTIAL_TAKEN || 
            pos.exit_stage == ExitStage::RUNNER_PROTECTED) {
            
            // Upgrade to trailing at +2R
            if (current_r >= config_.runner_trail_r && pos.exit_stage != ExitStage::TRAILING) {
                pos.exit_stage = ExitStage::TRAILING;
                printf("[INDEX-E2] %s Trail activated @ %.2fR\n", symbol, current_r);
            }
            
            // Update trailing stop
            if (pos.exit_stage == ExitStage::TRAILING) {
                double new_sl = pos.stop_loss;
                
                if (pos.side > 0 && pos.last_swing_low > pos.stop_loss) {
                    new_sl = pos.last_swing_low;
                } else if (pos.side < 0 && pos.last_swing_high > 0.0 && 
                           pos.last_swing_high < pos.stop_loss) {
                    new_sl = pos.last_swing_high;
                }
                
                if (new_sl != pos.stop_loss) {
                    printf("[INDEX-E2] %s Trail SL: %.2f -> %.2f\n", 
                           symbol, pos.stop_loss, new_sl);
                    pos.stop_loss = new_sl;
                }
            }
        }
        
        out.reason = "POSITION_MANAGED";
        return out;
    }
};

// =============================================================================
// GLOBAL INDEX IMPULSE ENGINE ACCESSOR
// =============================================================================
inline IndexImpulseEngine& getIndexImpulseEngine() {
    static IndexImpulseEngine instance;
    return instance;
}

} // namespace Chimera
