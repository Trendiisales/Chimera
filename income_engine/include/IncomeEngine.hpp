// =============================================================================
// IncomeEngine.hpp - Chimera 3rd Engine for Income/Yield Trading
// =============================================================================
// ARCHITECTURE:
//   - Completely separate from Alpha engines
//   - READ-ONLY inputs from Chimera infrastructure
//   - ML used ONLY as regime veto (not prediction)
//
// ML RULES (LOCKED):
//   - ML cannot trigger, ML cannot size, ML cannot override risk
//   - ML only vetoes
//   - If ML fails → veto all (safe default)
//
// SYMBOL RULES (LOCKED):
//   - NAS100 ONLY
//   - XAUUSD is HARD DISABLED
// =============================================================================
#pragma once

#include <atomic>
#include <functional>
#include <chrono>
#include <cmath>
#include <cstring>
#include <array>
#include <iostream>
#include <iomanip>

#include "IncomeRegimeFilter.hpp"
#include "shared/GlobalKill.hpp"
#include "shared/DailyLossGuard.hpp"
#include "risk/KillSwitchLadder.hpp"
#include "metrics/TradeOpportunityMetrics.hpp"

// v4.5.0: Engine-level symbol ownership enforcement
#include "core/EngineOwnership.hpp"
#include "shared/GlobalRiskGovernor.hpp"  // v4.5.1: Execution-layer enforcement

namespace Chimera {
namespace Income {

// =============================================================================
// Enums
// =============================================================================
enum class IncomeDirection : int8_t { NONE = 0, LONG = 1, SHORT = -1 };

enum class ExitReason : uint8_t {
    NONE = 0, TAKE_PROFIT, STOP_LOSS, TRAILING_STOP, 
    MAX_HOLD_TIME, KILL_SWITCH, VETO_EXIT, HARD_FAIL, MANUAL
};

inline const char* exit_reason_str(ExitReason r) {
    switch (r) {
        case ExitReason::TAKE_PROFIT:   return "TP";
        case ExitReason::STOP_LOSS:     return "SL";
        case ExitReason::TRAILING_STOP: return "TRAIL";
        case ExitReason::MAX_HOLD_TIME: return "TIME";
        case ExitReason::KILL_SWITCH:   return "KILL";
        case ExitReason::VETO_EXIT:     return "VETO";
        case ExitReason::HARD_FAIL:     return "HARDFAIL";
        case ExitReason::MANUAL:        return "MANUAL";
        default:                        return "NONE";
    }
}

enum class VetoReason : uint8_t {
    NONE = 0, ML_REGIME_UNSUITABLE, ML_FAILURE, SPREAD_WIDE, LIQUIDITY_LOW,
    SESSION_INVALID, COOLDOWN_ACTIVE, KILLSWITCH_ACTIVE, BOREDOM_BREACH,
    DAILY_LOSS_LIMIT, HARD_FAIL_ACTIVE, SYMBOL_NOT_ALLOWED
};

inline const char* veto_reason_str(VetoReason r) {
    switch (r) {
        case VetoReason::ML_REGIME_UNSUITABLE: return "REGIME_UNSUITABLE";
        case VetoReason::ML_FAILURE:           return "ML_FAILURE";
        case VetoReason::SPREAD_WIDE:          return "SPREAD_WIDE";
        case VetoReason::LIQUIDITY_LOW:        return "LIQUIDITY_LOW";
        case VetoReason::SESSION_INVALID:      return "SESSION_INVALID";
        case VetoReason::COOLDOWN_ACTIVE:      return "COOLDOWN";
        case VetoReason::KILLSWITCH_ACTIVE:    return "KILLSWITCH";
        case VetoReason::BOREDOM_BREACH:       return "BOREDOM_BREACH";
        case VetoReason::DAILY_LOSS_LIMIT:     return "DAILY_LOSS";
        case VetoReason::HARD_FAIL_ACTIVE:     return "HARD_FAIL";
        case VetoReason::SYMBOL_NOT_ALLOWED:   return "SYMBOL_BLOCKED";
        default:                               return "NONE";
    }
}

// =============================================================================
// ML Veto Log Entry (MANDATORY - NO SUPPRESSION)
// =============================================================================
struct MLVetoLog {
    char symbol[16] = {0};
    double score = 0.0;
    double threshold = 0.0;
    VetoReason reason = VetoReason::NONE;
    double vol_percentile = 0.0;
    double compression_ratio = 0.0;
    double spread_percentile = 0.0;
    double impulse_rate = 0.0;
    bool spread_unstable = false;
    
    void print() const {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm* utc = std::gmtime(&time);
        std::cout << "[INCOME][ML-VETO]\n"
                  << "  symbol=" << symbol << "\n"
                  << "  score=" << std::fixed << std::setprecision(2) << score << "\n"
                  << "  threshold=" << threshold << "\n"
                  << "  reason=" << veto_reason_str(reason) << "\n"
                  << "  features={vol_pct=" << (int)(vol_percentile * 100)
                  << ", compression=" << compression_ratio
                  << ", spread_unstable=" << (spread_unstable ? "TRUE" : "FALSE")
                  << ", impulse=" << (impulse_rate > 0.5 ? "HIGH" : "NORMAL") << "}\n"
                  << "  timestamp=" << std::put_time(utc, "%Y-%m-%dT%H:%M:%SZ") << "\n";
    }
};

// =============================================================================
// Trade Record (with MAE/MFE - MANDATORY)
// =============================================================================
struct TradeRecord {
    char symbol[16] = {0};
    IncomeDirection direction = IncomeDirection::NONE;
    double entry_price = 0.0, exit_price = 0.0, size = 0.0;
    int ticks_held = 0;
    double pnl_bps = 0.0, mae_bps = 0.0, mfe_bps = 0.0;
    ExitReason exit_reason = ExitReason::NONE;
    double regime_score_at_entry = 0.0;
    
    void print() const {
        std::cout << "[INCOME][TRADE] " << symbol 
                  << " " << (direction == IncomeDirection::LONG ? "LONG" : "SHORT")
                  << " PnL=" << std::fixed << std::setprecision(2) << pnl_bps << "bps"
                  << " MAE=" << mae_bps << " MFE=" << mfe_bps
                  << " exit=" << exit_reason_str(exit_reason)
                  << " ticks=" << ticks_held << "\n";
    }
};

// =============================================================================
// Position State
// =============================================================================
struct IncomePosition {
    char symbol[16] = {0};
    IncomeDirection direction = IncomeDirection::NONE;
    double entry_price = 0.0, size = 0.0;
    uint64_t entry_time_ns = 0;
    double unrealized_pnl_bps = 0.0;
    double max_favorable_bps = 0.0, max_adverse_bps = 0.0;
    int ticks_held = 0;
    double regime_score_at_entry = 0.0;
    
    bool is_flat() const noexcept { return direction == IncomeDirection::NONE; }
    bool is_long() const noexcept { return direction == IncomeDirection::LONG; }
    void reset() noexcept { *this = IncomePosition{}; }
};

struct IncomeSignal {
    IncomeDirection direction = IncomeDirection::NONE;
    double confidence = 0.0, edge_bps = 0.0;
    const char* reason = "";
    bool has_signal() const noexcept { return direction != IncomeDirection::NONE; }
};

// =============================================================================
// Session Statistics (SEPARATE PnL BUCKET - DO NOT MIX WITH ALPHA)
// =============================================================================
struct SessionStats {
    int trades_fired = 0, trades_vetoed = 0;
    int ml_vetoes = 0, spread_vetoes = 0, liquidity_vetoes = 0, session_vetoes = 0;
    int scratches = 0;
    double net_pnl_bps = 0.0, max_drawdown_bps = 0.0;
    double total_mae_bps = 0.0, total_mfe_bps = 0.0;
    void reset() noexcept { *this = SessionStats{}; }
    void print() const {
        std::cout << "[INCOME][SESSION] trades=" << trades_fired
                  << " vetoed=" << trades_vetoed << " ml_vetoes=" << ml_vetoes
                  << " pnl=" << std::fixed << std::setprecision(2) << net_pnl_bps << "bps"
                  << " maxDD=" << max_drawdown_bps << "bps\n";
    }
};

// =============================================================================
// Configuration
// =============================================================================
struct IncomeConfig {
    // SYMBOL LOCK - NAS100 ONLY, XAUUSD HARD DISABLED
    static constexpr const char* ALLOWED_SYMBOL = "NAS100";
    static constexpr bool XAUUSD_LOCKED_OUT = true;
    
    // Risk limits
    double max_position_size = 0.01;
    double max_daily_loss_bps = 50.0;
    double max_trade_loss_bps = 5.0;
    int max_trades_per_day = 6;  // 2-6 is healthy
    int boredom_threshold_trades = 10;  // RED FLAG if exceeded
    bool halt_on_boredom_breach = true;
    
    // Entry
    double min_compression_percentile = 0.3;
    double max_spread_percentile = 0.5;
    double min_edge_bps = 1.5;
    
    // Exit
    double take_profit_bps = 3.0;
    double stop_loss_bps = 5.0;
    double trail_start_bps = 2.0;
    double trail_distance_bps = 1.0;
    int max_hold_ticks = 500;
    int min_hold_ticks = 5;
    
    // Cooldowns
    int cooldown_after_loss_ms = 30000;
    int cooldown_after_win_ms = 5000;
    int cooldown_after_scratch_ms = 10000;
    
    // Sessions
    bool trade_asia = false;
    bool trade_london = true;
    bool trade_ny = true;
    
    // ML VETO (LOCKED AT 0.60)
    double ml_veto_threshold = 0.60;
    bool ml_failure_vetoes_all = true;
    bool log_all_vetoes = true;
    
    // Hard fail
    double hard_fail_daily_dd_pct = -0.50;
    int hard_fail_trades_per_session = 10;
    bool halt_on_hard_fail = true;
    
    // ═══════════════════════════════════════════════════════════════════════
    // LOCAL STAND-DOWN (BEHAVIOR-BASED CIRCUIT BREAKER)
    // ═══════════════════════════════════════════════════════════════════════
    // Trigger: 2 consecutive FAILED trades within 20 minutes
    // Action: Disable NAS100 income for 45 minutes
    // This is NOT ML, NOT risk - it's recent pain detection
    int stand_down_fail_count = 2;           // Consecutive fails to trigger
    int stand_down_window_ms = 20 * 60 * 1000;   // 20 minute rolling window
    int stand_down_duration_ms = 45 * 60 * 1000; // 45 minute lockout
    bool stand_down_enabled = true;          // DO NOT DISABLE
};

// =============================================================================
// Statistics
// =============================================================================
struct IncomeStats {
    std::atomic<uint64_t> ticks_processed{0}, signals_generated{0};
    std::atomic<uint64_t> trades_entered{0}, trades_exited{0};
    std::atomic<uint64_t> trades_won{0}, trades_lost{0}, trades_scratched{0};
    std::atomic<int64_t> total_pnl_bps{0}, total_mae_bps{0}, total_mfe_bps{0};
    std::atomic<int64_t> best_trade_bps{0}, worst_trade_bps{0};
    std::atomic<uint64_t> ml_vetoes{0}, spread_vetoes{0}, session_vetoes{0};
    std::atomic<uint64_t> cooldown_vetoes{0}, boredom_vetoes{0};
    std::atomic<uint64_t> stand_down_vetoes{0};       // NEW: stand-down blocks
    std::atomic<uint64_t> stand_down_triggers{0};    // NEW: times stand-down triggered
    std::atomic<uint64_t> exits_tp{0}, exits_sl{0}, exits_trail{0}, exits_time{0};
    std::atomic<bool> hard_fail_triggered{false};
    
    double win_rate() const noexcept {
        uint64_t t = trades_won.load() + trades_lost.load();
        return t > 0 ? double(trades_won.load()) / t : 0.0;
    }
    double avg_pnl_bps() const noexcept {
        uint64_t e = trades_exited.load();
        return e > 0 ? double(total_pnl_bps.load()) / 100.0 / e : 0.0;
    }
    double avg_mae_bps() const noexcept {
        uint64_t e = trades_exited.load();
        return e > 0 ? double(total_mae_bps.load()) / 100.0 / e : 0.0;
    }
    void reset() noexcept {
        ticks_processed = signals_generated = 0;
        trades_entered = trades_exited = trades_won = trades_lost = trades_scratched = 0;
        total_pnl_bps = total_mae_bps = total_mfe_bps = 0;
        best_trade_bps = worst_trade_bps = 0;
        ml_vetoes = spread_vetoes = session_vetoes = cooldown_vetoes = boredom_vetoes = 0;
        stand_down_vetoes = stand_down_triggers = 0;
        exits_tp = exits_sl = exits_trail = exits_time = 0;
        hard_fail_triggered = false;
    }
};

// =============================================================================
// Income Engine
// =============================================================================
class IncomeEngine {
public:
    using TradeCallback = std::function<void(const char*, int8_t, double, double, double)>;
    using VetoCallback = std::function<void(const MLVetoLog&)>;
    using LogCallback = std::function<void(const char*)>;
    
    IncomeEngine(GlobalKill& kill, DailyLossGuard& daily_loss) noexcept
        : kill_switch_(kill), daily_loss_(daily_loss) {}
    
    void set_config(const IncomeConfig& c) noexcept { config_ = c; }
    IncomeConfig& config() noexcept { return config_; }
    void set_trade_callback(TradeCallback cb) { trade_cb_ = std::move(cb); }
    void set_veto_callback(VetoCallback cb) { veto_cb_ = std::move(cb); }
    void set_log_callback(LogCallback cb) { log_cb_ = std::move(cb); }
    void set_killswitch_level(Omega::KillSwitchLevel l) noexcept { ks_level_.store(static_cast<int>(l)); }
    void set_crypto_stress(double s) noexcept { regime_filter_.set_crypto_stress(s); }
    
    bool start() {
        if (running_) return true;
        log("[INCOME] Starting - NAS100 ONLY, XAUUSD DISABLED, ML threshold=0.60");
        log("[INCOME] STAND-DOWN: 2 consecutive fails in 20min → 45min lockout");
        running_ = true; warmup_ = false; hard_fail_ = false;
        session_trades_ = 0;
        last_fail_time_ns_ = 0;
        consecutive_fails_ = 0;
        stand_down_until_ns_ = 0;
        stats_.reset(); session_stats_.reset(); regime_filter_.reset();
        return true;
    }
    
    void stop() {
        if (!running_) return;
        running_ = false;
        print_summary();
        log("[INCOME] Stopped");
    }
    
    bool is_running() const noexcept { return running_; }
    bool is_hard_fail() const noexcept { return hard_fail_; }
    const IncomeStats& stats() const noexcept { return stats_; }
    const SessionStats& session_stats() const noexcept { return session_stats_; }
    double current_regime_score() const noexcept { return regime_filter_.suitability_score(); }
    
    // v4.5.1: Check if any position is open (for cross-engine coordination)
    bool has_position() const noexcept {
        for (const auto& pos : positions_) {
            if (!pos.is_flat()) return true;
        }
        return false;
    }
    
    const char* idle_reason() const noexcept {
        if (hard_fail_) return "HARD_FAIL";
        if (!warmup_) return "WARMUP";
        
        // Check stand-down (this is checked BEFORE ML in real flow)
        uint64_t now_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        if (stand_down_until_ns_ > 0 && now_ns < stand_down_until_ns_) {
            int remaining_min = (stand_down_until_ns_ - now_ns) / 60000000000ULL;
            static char buf[64];
            snprintf(buf, sizeof(buf), "STAND-DOWN (%dm remaining)", remaining_min);
            return buf;
        }
        
        double s = regime_filter_.suitability_score();
        if (s < config_.ml_veto_threshold) {
            static char buf2[64];
            snprintf(buf2, sizeof(buf2), "ML veto (score %.2f)", s);
            return buf2;
        }
        return "WAITING";
    }
    
    void on_tick(const char* symbol, double bid, double ask, double bid_depth,
                 double ask_depth, double ofi, double vpin, uint64_t ts_ns) {
        if (!running_ || kill_switch_.killed() || hard_fail_) return;
        stats_.ticks_processed++;
        
        // SYMBOL GATE - NAS100 ONLY
        if (strcmp(symbol, IncomeConfig::ALLOWED_SYMBOL) != 0) return;
        
        // v4.5.0: ENGINE OWNERSHIP ENFORCEMENT (defense in depth)
        // This is redundant with the check above, but enforces at engine level
        if (!Chimera::EngineOwnership::instance().isAllowed(Chimera::EngineId::INCOME, symbol)) {
            // This should never fire given the check above, but if it does, log it
            printf("[ENGINE-BLOCK] IncomeEngine attempted forbidden symbol: %s\n", symbol);
            return;
        }
        
        double mid = (bid + ask) / 2.0;
        double spread_bps = ((ask - bid) / mid) * 10000.0;
        regime_filter_.on_tick(mid, spread_bps, bid_depth, ask_depth, ofi, vpin, ts_ns);
        
        if (!warmup_ && regime_filter_.is_warmed_up()) {
            warmup_ = true;
            log("[INCOME] Warmup complete");
        }
        
        int slot = find_slot(symbol);
        if (slot < 0) return;
        IncomePosition& pos = positions_[slot];
        
        if (!pos.is_flat()) {
            manage_position(pos, bid, ask, ts_ns);
            return;
        }
        
        // ENTRY GATES
        if (!warmup_) return;
        if (ks_level_.load() >= static_cast<int>(Omega::KillSwitchLevel::NO_NEW_ENTRY)) {
            log_veto(symbol, VetoReason::KILLSWITCH_ACTIVE, 0, "KS");
            return;
        }
        if (!daily_loss_.allow()) {
            log_veto(symbol, VetoReason::DAILY_LOSS_LIMIT, 0, "DL");
            return;
        }
        if (ts_ns < cooldown_until_ns_) {
            stats_.cooldown_vetoes++;
            return;
        }
        
        // ═══════════════════════════════════════════════════════════════════
        // STAND-DOWN CHECK (BEFORE ML - behavior-based circuit breaker)
        // ═══════════════════════════════════════════════════════════════════
        if (config_.stand_down_enabled && stand_down_until_ns_ > 0 && ts_ns < stand_down_until_ns_) {
            stats_.stand_down_vetoes++;
            // Log once per minute during stand-down
            static uint64_t last_sd_log = 0;
            if (ts_ns - last_sd_log > 60000000000ULL) {
                int remaining_min = (stand_down_until_ns_ - ts_ns) / 60000000000ULL;
                char buf[128];
                snprintf(buf, sizeof(buf), "[INCOME][STAND-DOWN-ACTIVE] symbol=%s remaining=%dm", 
                        symbol, remaining_min);
                log(buf);
                last_sd_log = ts_ns;
            }
            return;
        }
        // Check if stand-down just ended
        if (stand_down_until_ns_ > 0 && ts_ns >= stand_down_until_ns_) {
            log("[INCOME][STAND-DOWN-END] symbol=NAS100 - resuming");
            stand_down_until_ns_ = 0;
            consecutive_fails_ = 0;
        }
        
        if (session_trades_ >= config_.boredom_threshold_trades) {
            stats_.boredom_vetoes++;
            if (config_.halt_on_boredom_breach) trigger_hard_fail("BOREDOM_BREACH");
            return;
        }
        if (session_trades_ >= config_.max_trades_per_day) return;
        if (!is_valid_session()) {
            stats_.session_vetoes++;
            session_stats_.session_vetoes++;
            return;
        }
        
        // ML VETO (THE KEY FILTER)
        double score = regime_filter_.suitability_score();
        if (score < config_.ml_veto_threshold) {
            stats_.ml_vetoes++;
            session_stats_.ml_vetoes++;
            log_ml_veto(symbol, score, config_.ml_veto_threshold, VetoReason::ML_REGIME_UNSUITABLE);
            return;
        }
        
        // Spread check
        if (regime_filter_.compute_features().spread_percentile > config_.max_spread_percentile) {
            stats_.spread_vetoes++;
            session_stats_.spread_vetoes++;
            return;
        }
        
        // Signal
        IncomeSignal sig = generate_signal(ofi, vpin);
        if (!sig.has_signal() || sig.edge_bps < config_.min_edge_bps) return;
        
        stats_.signals_generated++;
        double size = config_.max_position_size * regime_filter_.size_scalar();
        double price = (sig.direction == IncomeDirection::LONG) ? ask : bid;
        execute_entry(pos, symbol, sig.direction, price, size, score, ts_ns);
    }
    
    void on_range_update(double range, double atr) noexcept {
        regime_filter_.on_range(range, atr);
    }
    
private:
    IncomeSignal generate_signal(double ofi, double vpin) const noexcept {
        IncomeSignal sig;
        if (ofi < -0.3 && vpin < 0.4) {
            sig.direction = IncomeDirection::LONG;
            sig.edge_bps = std::abs(ofi) * 5.0;
        } else if (ofi > 0.3 && vpin < 0.4) {
            sig.direction = IncomeDirection::SHORT;
            sig.edge_bps = std::abs(ofi) * 5.0;
        }
        return sig;
    }
    
    void manage_position(IncomePosition& pos, double bid, double ask, uint64_t ts_ns) {
        pos.ticks_held++;
        double exit_price = pos.is_long() ? bid : ask;
        double pnl_bps = ((pos.is_long() ? (exit_price - pos.entry_price) : (pos.entry_price - exit_price)) 
                         / pos.entry_price) * 10000.0;
        pos.unrealized_pnl_bps = pnl_bps;
        pos.max_favorable_bps = std::max(pos.max_favorable_bps, pnl_bps);
        pos.max_adverse_bps = std::min(pos.max_adverse_bps, pnl_bps);
        
        ExitReason reason = ExitReason::NONE;
        if (pnl_bps <= -config_.stop_loss_bps) reason = ExitReason::STOP_LOSS;
        else if (pnl_bps >= config_.take_profit_bps) reason = ExitReason::TAKE_PROFIT;
        else if (pos.max_favorable_bps >= config_.trail_start_bps &&
                 pnl_bps <= pos.max_favorable_bps - config_.trail_distance_bps)
            reason = ExitReason::TRAILING_STOP;
        else if (pos.ticks_held >= config_.max_hold_ticks) reason = ExitReason::MAX_HOLD_TIME;
        else if (kill_switch_.killed()) reason = ExitReason::KILL_SWITCH;
        else if (hard_fail_) reason = ExitReason::HARD_FAIL;
        
        if (reason != ExitReason::NONE && pos.ticks_held >= config_.min_hold_ticks)
            execute_exit(pos, exit_price, pnl_bps, reason, ts_ns);
    }
    
    void execute_entry(IncomePosition& pos, const char* symbol, IncomeDirection dir,
                       double price, double size, double score, uint64_t ts_ns) {
        // =====================================================================
        // v4.5.1 HARD EXECUTION GUARDS (NON-NEGOTIABLE - CHECKED FIRST)
        // These guards are at the execution boundary - NOTHING bypasses them
        // =====================================================================
        
        // GUARD 1: DAILY LOSS HARD STOP (-$200 NZD)
        // If tripped, NO orders go through - period.
        if (!Chimera::GlobalRiskGovernor::instance().canSubmitOrder(Chimera::EngineId::INCOME)) {
            char buf[128];
            snprintf(buf, sizeof(buf), "[INCOME][EXEC-BLOCKED] Entry BLOCKED - RISK GOVERNOR (daily loss or throttle)");
            log(buf);
            return;  // HARD STOP - nothing passes
        }
        
        // GUARD 2: NAS100 TIME-BASED OWNERSHIP
        // Income can only trade NAS100 during income window (03:00-05:00 NY)
        if (strcmp(symbol, "NAS100") == 0 && !Chimera::canTradeNAS100(Chimera::EngineId::INCOME)) {
            char buf[128];
            snprintf(buf, sizeof(buf), "[INCOME][EXEC-BLOCKED] NAS100 BLOCKED - not in income window");
            log(buf);
            return;  // HARD STOP - wrong time for NAS100
        }
        
        // GUARD 3: Engine ownership allowlist check
        if (!Chimera::EngineOwnership::instance().isAllowed(Chimera::EngineId::INCOME, symbol)) {
            char buf[128];
            snprintf(buf, sizeof(buf), "[INCOME][ENGINE-BLOCK] Entry BLOCKED for %s - not in allowed list", symbol);
            log(buf);
            return;
        }
        
        strncpy(pos.symbol, symbol, 15);
        pos.direction = dir;
        pos.entry_price = price;
        pos.size = size;
        pos.entry_time_ns = ts_ns;
        pos.regime_score_at_entry = score;
        pos.max_favorable_bps = pos.max_adverse_bps = pos.unrealized_pnl_bps = 0;
        pos.ticks_held = 0;
        
        stats_.trades_entered++;
        session_trades_++;
        session_stats_.trades_fired++;
        
        if (trade_cb_) trade_cb_(symbol, dir == IncomeDirection::LONG ? 1 : -1, size, price, 0);
        
        // v4.5.0: Include engine_id in log for attribution
        char buf[128];
        snprintf(buf, sizeof(buf), "[INCOME] engine=INCOME ENTRY %s %s size=%.4f price=%.2f regime=%.2f",
                symbol, dir == IncomeDirection::LONG ? "LONG" : "SHORT", size, price, score);
        log(buf);
    }
    
    void execute_exit(IncomePosition& pos, double price, double pnl_bps, ExitReason reason, uint64_t ts_ns) {
        stats_.trades_exited++;
        stats_.total_pnl_bps.fetch_add(static_cast<int64_t>(pnl_bps * 100));
        stats_.total_mae_bps.fetch_add(static_cast<int64_t>(pos.max_adverse_bps * 100));
        stats_.total_mfe_bps.fetch_add(static_cast<int64_t>(pos.max_favorable_bps * 100));
        
        session_stats_.net_pnl_bps += pnl_bps;
        if (session_stats_.net_pnl_bps < session_stats_.max_drawdown_bps)
            session_stats_.max_drawdown_bps = session_stats_.net_pnl_bps;
        
        if (session_stats_.net_pnl_bps / 100.0 < config_.hard_fail_daily_dd_pct)
            trigger_hard_fail("DD_BREACH");
        
        if (pnl_bps > 1) stats_.trades_won++;
        else if (pnl_bps < -1) stats_.trades_lost++;
        else { stats_.trades_scratched++; session_stats_.scratches++; }
        
        switch (reason) {
            case ExitReason::TAKE_PROFIT: stats_.exits_tp++; break;
            case ExitReason::STOP_LOSS: stats_.exits_sl++; break;
            case ExitReason::TRAILING_STOP: stats_.exits_trail++; break;
            case ExitReason::MAX_HOLD_TIME: stats_.exits_time++; break;
            default: break;
        }
        
        // ═══════════════════════════════════════════════════════════════════
        // STAND-DOWN FAIL TRACKING
        // ═══════════════════════════════════════════════════════════════════
        // A trade is a FAIL if:
        //   - Hit STOP_LOSS
        //   - Exited via TIMEOUT with negative PnL
        // NOT a fail: scratch, TP, timeout with >=0 PnL
        bool is_fail = (reason == ExitReason::STOP_LOSS) ||
                       (reason == ExitReason::MAX_HOLD_TIME && pnl_bps < -1.0);
        
        if (config_.stand_down_enabled) {
            if (is_fail) {
                // Check if within rolling window of last fail
                uint64_t window_ns = config_.stand_down_window_ms * 1000000ULL;
                if (last_fail_time_ns_ > 0 && (ts_ns - last_fail_time_ns_) <= window_ns) {
                    consecutive_fails_++;
                } else {
                    consecutive_fails_ = 1;  // First fail or outside window
                }
                last_fail_time_ns_ = ts_ns;
                
                char buf[128];
                snprintf(buf, sizeof(buf), "[INCOME][FAIL] reason=%s pnl=%.2fbps consecutive=%d",
                        exit_reason_str(reason), pnl_bps, consecutive_fails_);
                log(buf);
                
                // Check if we should trigger stand-down
                if (consecutive_fails_ >= config_.stand_down_fail_count) {
                    trigger_stand_down(ts_ns);
                }
            } else {
                // Successful trade - reset fail counter
                if (pnl_bps > 1.0) {  // Only reset on actual wins, not scratches
                    consecutive_fails_ = 0;
                }
            }
        }
        
        int cd_ms = pnl_bps > 1 ? config_.cooldown_after_win_ms :
                   pnl_bps < -1 ? config_.cooldown_after_loss_ms : config_.cooldown_after_scratch_ms;
        cooldown_until_ns_ = ts_ns + cd_ms * 1000000ULL;
        
        if (trade_cb_) trade_cb_(pos.symbol, pos.is_long() ? -1 : 1, pos.size, price, pnl_bps);
        
        TradeRecord rec;
        memcpy(rec.symbol, pos.symbol, sizeof(rec.symbol) - 1);
        rec.symbol[sizeof(rec.symbol) - 1] = '\0';
        rec.direction = pos.direction;
        rec.entry_price = pos.entry_price;
        rec.exit_price = price;
        rec.size = pos.size;
        rec.ticks_held = pos.ticks_held;
        rec.pnl_bps = pnl_bps;
        rec.mae_bps = pos.max_adverse_bps;
        rec.mfe_bps = pos.max_favorable_bps;
        rec.exit_reason = reason;
        rec.regime_score_at_entry = pos.regime_score_at_entry;
        rec.print();
        
        pos.reset();
    }
    
    void log_ml_veto(const char* symbol, double score, double thresh, VetoReason reason) {
        MLVetoLog v;
        strncpy(v.symbol, symbol, 15);
        v.score = score;
        v.threshold = thresh;
        v.reason = reason;
        auto f = regime_filter_.compute_features();
        v.vol_percentile = f.vol_percentile;
        v.compression_ratio = f.range_compression;
        v.spread_percentile = f.spread_percentile;
        v.impulse_rate = f.impulse_frequency;
        v.spread_unstable = f.spread_stability > 0.5;
        v.print();
        if (veto_cb_) veto_cb_(v);
        session_stats_.trades_vetoed++;
    }
    
    void log_veto(const char* symbol, VetoReason reason, double score, const char* ctx) {
        session_stats_.trades_vetoed++;
        char buf[128];
        snprintf(buf, sizeof(buf), "[INCOME][VETO] %s %s score=%.2f %s",
                symbol, veto_reason_str(reason), score, ctx);
        log(buf);
    }
    
    void trigger_hard_fail(const char* reason) {
        if (hard_fail_) return;
        hard_fail_ = true;
        stats_.hard_fail_triggered = true;
        char buf[128];
        snprintf(buf, sizeof(buf), "[INCOME][HARD-FAIL] %s - HALTED", reason);
        log(buf);
    }
    
    void trigger_stand_down(uint64_t ts_ns) {
        stand_down_until_ns_ = ts_ns + config_.stand_down_duration_ms * 1000000ULL;
        stats_.stand_down_triggers++;
        
        int duration_min = config_.stand_down_duration_ms / 60000;
        int window_min = config_.stand_down_window_ms / 60000;
        
        char buf[256];
        snprintf(buf, sizeof(buf), 
            "[INCOME][STAND-DOWN-START]\n"
            "  symbol=NAS100\n"
            "  reason=%d_consecutive_failures\n"
            "  window=%dm\n"
            "  duration=%dm",
            config_.stand_down_fail_count, window_min, duration_min);
        log(buf);
        
        // Reset fail counter after triggering
        consecutive_fails_ = 0;
    }
    
    int find_slot(const char* symbol) noexcept {
        for (size_t i = 0; i < 5; i++)
            if (strcmp(positions_[i].symbol, symbol) == 0) return i;
        for (size_t i = 0; i < 5; i++)
            if (positions_[i].symbol[0] == '\0') {
                strncpy(positions_[i].symbol, symbol, 15);
                return i;
            }
        return -1;
    }
    
    bool is_valid_session() const noexcept {
        auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm* utc = std::gmtime(&t);
        if (!utc || utc->tm_wday == 0 || utc->tm_wday == 6) return false;
        int h = utc->tm_hour;
        if (h == 8 || h == 13 || h == 14) return false;  // Block opens
        bool london = h >= 9 && h < 16;
        bool ny = h >= 15 && h < 21;
        return (london && config_.trade_london) || (ny && config_.trade_ny);
    }
    
    void print_summary() const {
        char buf[256];
        log("[INCOME] ═══════════════════════════════════════════════");
        snprintf(buf, sizeof(buf), "[INCOME] Trades: %d fired, %d vetoed, ML vetoes: %d",
                session_stats_.trades_fired, session_stats_.trades_vetoed, session_stats_.ml_vetoes);
        log(buf);
        snprintf(buf, sizeof(buf), "[INCOME] Stand-downs: %lu triggered, %lu vetoes",
                stats_.stand_down_triggers.load(), stats_.stand_down_vetoes.load());
        log(buf);
        snprintf(buf, sizeof(buf), "[INCOME] PnL: %.2f bps, MaxDD: %.2f bps",
                session_stats_.net_pnl_bps, session_stats_.max_drawdown_bps);
        log(buf);
        snprintf(buf, sizeof(buf), "[INCOME] Avg MAE: %.2f bps, Avg MFE: %.2f bps",
                stats_.avg_mae_bps(), double(stats_.total_mfe_bps.load()) / 100.0 / 
                std::max(1UL, stats_.trades_exited.load()));
        log(buf);
        if (session_stats_.trades_fired == 0) log("[INCOME] ✓ Zero trades - ACCEPTABLE");
        else if (session_stats_.trades_fired <= 4) log("[INCOME] ✓ Low trades - HEALTHY");
        else if (session_stats_.trades_fired <= 6) log("[INCOME] ⚠ Moderate - MONITOR");
        else log("[INCOME] ❌ HIGH trades - INVESTIGATE");
        log("[INCOME] ═══════════════════════════════════════════════");
    }
    
    void log(const char* msg) const {
        if (log_cb_) log_cb_(msg);
        else std::cout << msg << std::endl;
    }
    
    GlobalKill& kill_switch_;
    DailyLossGuard& daily_loss_;
    IncomeConfig config_;
    std::atomic<bool> running_{false}, warmup_{false}, hard_fail_{false};
    std::atomic<int> ks_level_{0};
    std::array<IncomePosition, 5> positions_;
    IncomeRegimeFilter regime_filter_;
    IncomeStats stats_;
    SessionStats session_stats_;
    int session_trades_ = 0;
    uint64_t cooldown_until_ns_ = 0;
    
    // STAND-DOWN STATE (behavior-based circuit breaker)
    uint64_t last_fail_time_ns_ = 0;      // Timestamp of last failed trade
    int consecutive_fails_ = 0;            // Count of consecutive fails
    uint64_t stand_down_until_ns_ = 0;     // Stand-down active until this time
    
    TradeCallback trade_cb_;
    VetoCallback veto_cb_;
    LogCallback log_cb_;
};

} // namespace Income
} // namespace Chimera
