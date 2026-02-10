#pragma once
// =============================================================================
// MetalMicroScalp.hpp - Precious Metals Micro-Scalping Engine
// =============================================================================
// v4.9.6: 3-STATE LIFECYCLE FOR GOLD & SILVER
//
// CRITICAL DIFFERENCES FROM CRYPTO:
//   - Session gating: London open → NY mid (07:00-17:00 UTC)
//   - Gold lies early, then trends - needs structure confirmation
//   - Silver is dangerous - opportunistic only, not core
//   - Stricter kill switches (2 losses for silver, 3 for gold)
//
// GOLD (XAUUSD):
//   - Entry edge: OFI * 0.6 + pressure * 0.3 + micro_trend * 0.4
//   - Patient probe (70-180ms), patient exit
//   - If slope and OFI disagree → NO TRADE
//
// SILVER (XAGUSD):
//   - Entry edge: OFI * 0.5 + pressure * 0.4 + spread_compression * 0.3
//   - Brutal filters (50-120ms probe)
//   - If spread not compressing → NO TRADE
//   - 2 losses → DISABLE (not 3)
// =============================================================================

#include <string>
#include <cstdint>
#include <chrono>
#include <functional>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <ctime>

namespace Chimera {
namespace Metal {

// =============================================================================
// THE 3-STATE LIFECYCLE (same as crypto)
// =============================================================================
enum class MicroState : uint8_t {
    FLAT    = 0,  // No position
    PROBE   = 1,  // Evaluating post-entry structure
    CONFIRM = 2   // Winner window - holding
};

inline const char* microStateStr(MicroState s) {
    switch (s) {
        case MicroState::FLAT:    return "FLAT";
        case MicroState::PROBE:   return "PROBE";
        case MicroState::CONFIRM: return "CONFIRM";
    }
    return "UNK";
}

// =============================================================================
// Metal Regime
// =============================================================================
enum class MetalRegime : uint8_t {
    DEAD    = 0,  // Outside session or no edge
    STABLE  = 1,  // Normal conditions
    TREND   = 2,  // Directional move
    SPIKE   = 3   // Volatility spike - avoid
};

inline const char* metalRegimeStr(MetalRegime r) {
    switch (r) {
        case MetalRegime::DEAD:   return "DEAD";
        case MetalRegime::STABLE: return "STABLE";
        case MetalRegime::TREND:  return "TREND";
        case MetalRegime::SPIKE:  return "SPIKE";
    }
    return "UNK";
}

// =============================================================================
// Fill Type
// =============================================================================
enum class FillType : uint8_t {
    UNKNOWN = 0,
    MAKER   = 1,
    TAKER   = 2
};

inline const char* fillTypeStr(FillType f) {
    switch (f) {
        case FillType::MAKER:  return "MAKER";
        case FillType::TAKER:  return "TAKER";
        default:               return "UNK";
    }
}

// =============================================================================
// Routing Mode
// =============================================================================
enum class RoutingMode : uint8_t {
    TAKER_ONLY  = 0,
    MAKER_FIRST = 1,
    MAKER_ONLY  = 2
};

// =============================================================================
// Exit Reason
// =============================================================================
enum class ExitReason : uint8_t {
    NONE = 0,
    SPREAD_EXPAND,
    EDGE_DECAY,
    PROBE_TIMEOUT,
    TAKE_PROFIT,
    STOP_LOSS,
    TIME_STOP,
    SPREAD_BREAK,
    SESSION_CLOSE,
    OFI_FLIP,
    MANUAL
};

inline const char* exitReasonStr(ExitReason r) {
    switch (r) {
        case ExitReason::SPREAD_EXPAND: return "SPREAD_EXPAND";
        case ExitReason::EDGE_DECAY:    return "EDGE_DECAY";
        case ExitReason::PROBE_TIMEOUT: return "PROBE_TIMEOUT";
        case ExitReason::TAKE_PROFIT:   return "TAKE_PROFIT";
        case ExitReason::STOP_LOSS:     return "STOP_LOSS";
        case ExitReason::TIME_STOP:     return "TIME_STOP";
        case ExitReason::SPREAD_BREAK:  return "SPREAD_BREAK";
        case ExitReason::SESSION_CLOSE: return "SESSION_CLOSE";
        case ExitReason::OFI_FLIP:      return "OFI_FLIP";
        default: return "UNK";
    }
}

// =============================================================================
// Symbol Type
// =============================================================================
enum class MetalSymbol : uint8_t {
    XAUUSD = 0,  // Gold
    XAGUSD = 1,  // Silver
    OTHER  = 255
};

inline MetalSymbol parseMetalSymbol(const char* sym) {
    if (strcmp(sym, "XAUUSD") == 0) return MetalSymbol::XAUUSD;
    if (strcmp(sym, "XAGUSD") == 0) return MetalSymbol::XAGUSD;
    return MetalSymbol::OTHER;
}

// =============================================================================
// Fee Configuration (CFD spreads are different)
// =============================================================================
struct MetalFeeConfig {
    double spread_cost_mult = 0.5;  // Assume half spread on entry
    double commission_bps = 0.0;    // Most CFD = spread only
    uint32_t maker_timeout_ms = 60;
};

inline MetalFeeConfig getMetalFeeConfig(const char* symbol) {
    MetalFeeConfig cfg;
    if (strcmp(symbol, "XAUUSD") == 0) {
        cfg.spread_cost_mult = 0.5;
        cfg.commission_bps = 0.0;
        cfg.maker_timeout_ms = 60;
    } else if (strcmp(symbol, "XAGUSD") == 0) {
        cfg.spread_cost_mult = 0.5;
        cfg.commission_bps = 0.0;
        cfg.maker_timeout_ms = 50;
    }
    return cfg;
}

// =============================================================================
// SYMBOL-SPECIFIC PROBE PARAMETERS
// =============================================================================
struct MetalProbeParams {
    // Entry
    double entry_edge_bps;
    double base_tp_bps;
    double sl_bps;
    
    // Probe
    int probe_min_ms;
    int probe_max_ms;
    double spread_expand_mult;
    double edge_drop_ratio;
    double vol_confirm_mult;
    
    // Confirm
    int min_hold_ms;
    int max_hold_ms;
    double tp_expansion;
    
    // Risk
    double daily_loss_cap_bps;
    int max_loss_streak;
};

inline MetalProbeParams getMetalProbeParams(const char* symbol) {
    MetalProbeParams p;
    
    if (strcmp(symbol, "XAUUSD") == 0) {
        // Gold: patient entry, patient exit
        p.entry_edge_bps = 0.45;
        p.base_tp_bps = 0.80;
        p.sl_bps = 0.40;
        
        p.probe_min_ms = 70;
        p.probe_max_ms = 180;
        p.spread_expand_mult = 1.30;
        p.edge_drop_ratio = 0.55;
        p.vol_confirm_mult = 1.45;
        
        p.min_hold_ms = 150;
        p.max_hold_ms = 650;
        p.tp_expansion = 0.6;
        
        p.daily_loss_cap_bps = -0.30;
        p.max_loss_streak = 3;
    }
    else if (strcmp(symbol, "XAGUSD") == 0) {
        // Silver: brutal filters, opportunistic only
        p.entry_edge_bps = 0.70;
        p.base_tp_bps = 1.10;
        p.sl_bps = 0.45;
        
        p.probe_min_ms = 50;
        p.probe_max_ms = 120;
        p.spread_expand_mult = 1.20;  // Tighter than gold
        p.edge_drop_ratio = 0.65;     // Less tolerance
        p.vol_confirm_mult = 1.60;    // Higher bar
        
        p.min_hold_ms = 120;
        p.max_hold_ms = 450;
        p.tp_expansion = 0.8;  // Silver winners are rare but large
        
        p.daily_loss_cap_bps = -0.20;
        p.max_loss_streak = 2;  // HARDER LIMIT for silver
    }
    else {
        // Default (shouldn't be used)
        p.entry_edge_bps = 0.50;
        p.base_tp_bps = 0.90;
        p.sl_bps = 0.45;
        p.probe_min_ms = 60;
        p.probe_max_ms = 150;
        p.spread_expand_mult = 1.25;
        p.edge_drop_ratio = 0.60;
        p.vol_confirm_mult = 1.50;
        p.min_hold_ms = 140;
        p.max_hold_ms = 550;
        p.tp_expansion = 0.7;
        p.daily_loss_cap_bps = -0.25;
        p.max_loss_streak = 3;
    }
    
    return p;
}

// =============================================================================
// SYMBOL-SPECIFIC ENTRY PARAMETERS (v4.9.6 - Per-engine gates)
// =============================================================================
// KEY INSIGHT: Gold and Silver have DIFFERENT trading conditions
// Gold: Trades in STABLE regime, needs trend confirmation
// Silver: Only in TREND regime, needs spread compression
// =============================================================================
struct MetalEntryParams {
    double min_confidence;        // Minimum regime confidence to trade
    double min_ofi;               // Minimum |OFI| for entry
    double min_edge_bps;          // Minimum edge after costs
    bool allow_stable_regime;     // Can trade when regime == STABLE?
    bool require_trend_confirm;   // Must trend tracker agree?
    bool require_spread_compress; // Must spread be compressing?
    uint32_t warmup_ticks;        // Per-engine warmup
};

inline MetalEntryParams getMetalEntryParams(const char* symbol) {
    MetalEntryParams p;
    
    if (strcmp(symbol, "XAUUSD") == 0) {
        // Gold: More patient, can trade in STABLE
        // Gold lies early then trends - needs structure confirmation
        p.min_confidence = 0.50;
        p.min_ofi = 0.30;
        p.min_edge_bps = 0.45;
        p.allow_stable_regime = true;     // Gold CAN trade stable
        p.require_trend_confirm = true;   // Must have trend alignment
        p.require_spread_compress = false;
        p.warmup_ticks = 500;             // Needs spread medians
    }
    else if (strcmp(symbol, "XAGUSD") == 0) {
        // Silver: Dangerous - opportunistic only
        // Only when spread compressing AND clear direction
        p.min_confidence = 0.65;          // Higher bar
        p.min_ofi = 0.40;                 // Needs strong flow
        p.min_edge_bps = 0.70;            // Higher edge requirement
        p.allow_stable_regime = false;    // Silver needs TREND
        p.require_trend_confirm = true;
        p.require_spread_compress = true; // Must have compression
        p.warmup_ticks = 300;             // Faster warmup
    }
    else {
        // Default
        p.min_confidence = 0.55;
        p.min_ofi = 0.35;
        p.min_edge_bps = 0.50;
        p.allow_stable_regime = true;
        p.require_trend_confirm = true;
        p.require_spread_compress = false;
        p.warmup_ticks = 400;
    }
    
    return p;
}

// =============================================================================
// Entry Snapshot
// =============================================================================
struct EntrySnapshot {
    double price;
    double edge_bps;
    double spread_bps;
    double vol;
    double ofi;
    uint64_t ts_ns;
    FillType fill_type;
};

// =============================================================================
// Tick data (metals version)
// =============================================================================
struct MetalTick {
    double bid;
    double ask;
    double mid;
    double ofi;           // Order flow imbalance [-1, +1]
    double pressure;      // Buy/sell pressure
    double volatility;    // Realized vol
    double spread_bps;    // Current spread in bps
    uint64_t ts_ns;
};

// =============================================================================
// Callbacks
// =============================================================================
using MetalTradeCallback = std::function<void(
    const char* symbol, int8_t side, double qty, double price, double pnl_bps
)>;

using MetalOrderCallback = std::function<void(
    const char* symbol, bool is_buy, double qty, double price, RoutingMode routing
)>;

// =============================================================================
// Spread Tracker
// =============================================================================
class MetalSpreadTracker {
public:
    void onTick(double spread_bps) {
        spreads_.push_back(spread_bps);
        if (spreads_.size() > WINDOW_SIZE) spreads_.pop_front();
        
        if (spreads_.size() >= MIN_SAMPLES) {
            std::vector<double> sorted(spreads_.begin(), spreads_.end());
            std::sort(sorted.begin(), sorted.end());
            median_spread_ = sorted[sorted.size() / 2];
        }
    }
    
    double medianSpread() const { return median_spread_; }
    
    bool isCompressing() const {
        if (spreads_.size() < 10) return false;
        double recent_avg = 0.0;
        int count = 0;
        for (auto it = spreads_.rbegin(); it != spreads_.rend() && count < 5; ++it, ++count) {
            recent_avg += *it;
        }
        recent_avg /= count;
        return recent_avg < median_spread_ * 0.9;
    }
    
    bool isAnomaly(double spread_bps) const {
        return median_spread_ > 0 && spread_bps > median_spread_ * 1.5;
    }
    
private:
    static constexpr size_t WINDOW_SIZE = 100;
    static constexpr size_t MIN_SAMPLES = 20;
    std::deque<double> spreads_;
    double median_spread_ = 0.0;
};

// =============================================================================
// Micro Trend Tracker (for Gold edge calculation)
// =============================================================================
class MicroTrendTracker {
public:
    void onTick(double mid, uint64_t ts_ns) {
        prices_.push_back({mid, ts_ns});
        
        // Keep last 10 seconds of data
        while (!prices_.empty() && ts_ns - prices_.front().ts_ns > 10'000'000'000ULL) {
            prices_.pop_front();
        }
        
        // Calculate slope over last 5-10 seconds
        if (prices_.size() >= 10) {
            double first_price = prices_.front().price;
            double last_price = prices_.back().price;
            double duration_sec = (prices_.back().ts_ns - prices_.front().ts_ns) / 1e9;
            
            if (duration_sec > 0.5) {
                slope_ = (last_price - first_price) / first_price / duration_sec;
            }
        }
    }
    
    // Returns normalized trend bias [-1, +1]
    double trendBias() const {
        // Convert slope to normalized value
        // Typical gold micro-move is ~0.0001% per second
        double normalized = slope_ * 10000.0;  // Scale up
        return std::max(-1.0, std::min(1.0, normalized));
    }
    
    // Check if slope agrees with OFI direction
    bool agreesWithOFI(double ofi) const {
        if (std::fabs(slope_) < 1e-8) return true;  // No clear slope
        return (slope_ > 0 && ofi > 0) || (slope_ < 0 && ofi < 0);
    }
    
private:
    struct PricePoint { double price; uint64_t ts_ns; };
    std::deque<PricePoint> prices_;
    double slope_ = 0.0;
};

// =============================================================================
// Loss Cluster Tracker
// =============================================================================
class MetalLossTracker {
public:
    void recordTrade(bool is_loss, double pnl_bps, [[maybe_unused]] uint64_t ts_ns) {
        if (is_loss) {
            loss_streak_++;
            daily_pnl_bps_ += pnl_bps;
        } else {
            loss_streak_ = 0;
        }
        total_pnl_bps_ += pnl_bps;
    }
    
    int lossStreak() const { return loss_streak_; }
    double dailyPnlBps() const { return daily_pnl_bps_; }
    double totalPnlBps() const { return total_pnl_bps_; }
    
    void resetDaily() {
        loss_streak_ = 0;
        daily_pnl_bps_ = 0.0;
    }
    
private:
    int loss_streak_ = 0;
    double daily_pnl_bps_ = 0.0;
    double total_pnl_bps_ = 0.0;
};

// =============================================================================
// Session Detector - London open → NY mid (07:00-17:00 UTC)
// =============================================================================
class MetalSessionDetector {
public:
    bool isSessionActive() const {
        time_t now = time(nullptr);
        struct tm* utc = gmtime(&now);
        int hour = utc->tm_hour;
        int dow = utc->tm_wday;  // 0 = Sunday
        
        // No trading on weekends
        if (dow == 0 || dow == 6) return false;
        
        // Session: 07:00 - 17:00 UTC
        return hour >= 7 && hour < 17;
    }
    
    bool isNearSessionEnd() const {
        time_t now = time(nullptr);
        struct tm* utc = gmtime(&now);
        int hour = utc->tm_hour;
        int minute = utc->tm_min;
        
        // Within 15 minutes of session end (16:45-17:00)
        return hour == 16 && minute >= 45;
    }
    
    const char* sessionStatus() const {
        if (!isSessionActive()) return "CLOSED";
        if (isNearSessionEnd()) return "CLOSING";
        return "OPEN";
    }
};

// =============================================================================
// Regime Detector (Metals)
// =============================================================================
class MetalRegimeDetector {
public:
    void onTick(double mid, double volatility, double ofi) {
        if (last_mid_ > 0.0) {
            double move = std::fabs(mid - last_mid_) / last_mid_;
            ema_move_ = 0.05 * move + 0.95 * ema_move_;
        }
        last_mid_ = mid;
        last_vol_ = volatility;
        last_ofi_ = ofi;
        updateConfidence();
    }
    
    MetalRegime regime() const {
        if (ema_move_ > 0.0005) return MetalRegime::SPIKE;  // Too volatile
        if (ema_move_ < 0.00005) return MetalRegime::STABLE;
        if (std::fabs(last_ofi_) > 0.5) return MetalRegime::TREND;
        return MetalRegime::STABLE;
    }
    
    double confidence() const { return confidence_; }
    bool isSpike() const { return regime() == MetalRegime::SPIKE; }
    
private:
    void updateConfidence() {
        double vol_score = 1.0;
        if (regime() == MetalRegime::SPIKE) vol_score = 0.3;
        double ofi_score = std::min(1.0, std::fabs(last_ofi_) * 1.5);
        confidence_ = (vol_score * 0.5 + ofi_score * 0.5);
    }
    
    double last_mid_ = 0.0, ema_move_ = 0.0, last_vol_ = 0.0, last_ofi_ = 0.0;
    double confidence_ = 0.0;
};

// =============================================================================
// PnL Attribution
// =============================================================================
struct MetalPnLAttribution {
    double raw_pnl_bps;
    double spread_cost;
    double net_pnl_bps;
    FillType entry_fill;
    FillType exit_fill;
    
    void compute(double entry_price, double exit_price, double entry_spread_bps,
                 FillType entry_type, FillType exit_type, const MetalFeeConfig& fees) {
        raw_pnl_bps = (exit_price - entry_price) / entry_price * 10000.0;
        spread_cost = entry_spread_bps * fees.spread_cost_mult + fees.commission_bps;
        net_pnl_bps = raw_pnl_bps - spread_cost;
        entry_fill = entry_type;
        exit_fill = exit_type;
    }
    
    void log(const char* symbol, ExitReason reason) const {
        printf("[METAL-MICROSCALP-PNL] %s raw=%.2f spread=-%.2f net=%.2f fills=%s/%s reason=%s\n",
               symbol, raw_pnl_bps, spread_cost, net_pnl_bps,
               fillTypeStr(entry_fill), fillTypeStr(exit_fill), exitReasonStr(reason));
    }
};

// =============================================================================
// MetalMicroScalpEngine - THE CORE
// =============================================================================
class MetalMicroScalpEngine {
public:
    explicit MetalMicroScalpEngine(const std::string& symbol);
    
    void onTick(const MetalTick& tick);
    void onFill(FillType fill_type, double filled_price);
    void onMakerTimeout();
    
    // Callbacks
    void setOrderCallback(MetalOrderCallback cb) { order_cb_ = std::move(cb); }
    void setTradeCallback(MetalTradeCallback cb) { trade_cb_ = std::move(cb); }
    
    // Control
    void setEnabled(bool e) { enabled_ = e; }
    bool isEnabled() const { return enabled_; }
    void setBaseQty(double q) { base_qty_ = q; }
    void setRoutingMode(RoutingMode m) { routing_mode_ = m; }
    
    // Stats
    MicroState currentState() const { return state_; }
    uint64_t tradesEntered() const { return trades_entered_; }
    uint64_t tradesExited() const { return trades_exited_; }
    uint64_t probeFailures() const { return probe_failures_; }
    uint64_t probeConfirms() const { return probe_confirms_; }
    double totalPnlBps() const { return loss_tracker_.totalPnlBps(); }
    double winRate() const { 
        return trades_exited_ > 0 ? (double)winning_trades_ / trades_exited_ : 0.0; 
    }
    
    // Regime
    MetalRegime currentRegime() const { return regime_detector_.regime(); }
    double regimeConfidence() const { return regime_detector_.confidence(); }
    
    // Session
    bool isSessionActive() const { return session_.isSessionActive(); }
    const char* sessionStatus() const { return session_.sessionStatus(); }
    
    // Kill state
    bool isAutoDisabled() const { return auto_disabled_; }
    const char* disableReason() const { return disable_reason_; }
    void resetSession() {
        loss_tracker_.resetDaily();
        auto_disabled_ = false;
        disable_reason_ = "";
    }
    
    static constexpr uint8_t ENGINE_ID = 3;  // MetalMicroScalp engine ID
    static constexpr uint8_t STRATEGY_ID = 10;  // Metal scalp strategy

private:
    // State machine handlers
    void handleFlat(const MetalTick& tick);
    void handleProbe(const MetalTick& tick);
    void handleConfirm(const MetalTick& tick);
    
    // Entry/Exit
    bool checkEntryFilters(const MetalTick& tick, double edge_bps);
    void enter(const MetalTick& tick, double edge_bps);
    void exit(const MetalTick& tick, ExitReason reason);
    
    // Edge calculation (symbol-specific)
    double calcEdgeBps(const MetalTick& tick);
    double calcPnlBps(const MetalTick& tick) const;
    double totalCostBps(const MetalTick& tick) const;
    
    // Kill switch check
    void checkKillConditions(double pnl_bps);
    
    static inline uint64_t nowNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }

private:
    std::string symbol_;
    MetalSymbol symbol_type_;
    MetalRegimeDetector regime_detector_;
    MetalSpreadTracker spread_tracker_;
    MicroTrendTracker trend_tracker_;
    MetalLossTracker loss_tracker_;
    MetalSessionDetector session_;
    MetalFeeConfig fee_config_;
    MetalProbeParams probe_params_;
    MetalEntryParams entry_params_;  // v4.9.6: Per-symbol entry thresholds
    
    // STATE MACHINE
    MicroState state_ = MicroState::FLAT;
    EntrySnapshot snapshot_{};
    uint64_t confirm_ts_ns_ = 0;
    double last_edge_bps_ = 0.0;
    double entry_ofi_ = 0.0;  // For OFI flip detection (silver)
    
    // Position
    bool long_side_ = true;
    FillType entry_fill_type_ = FillType::UNKNOWN;
    FillType exit_fill_type_ = FillType::UNKNOWN;
    bool pending_fill_ = false;
    
    // Callbacks
    MetalOrderCallback order_cb_;
    MetalTradeCallback trade_cb_;
    
    // Config
    bool enabled_ = true;
    double base_qty_ = 0.01;  // Gold: 0.01 lot, Silver: 0.1 lot typical
    RoutingMode routing_mode_ = RoutingMode::MAKER_FIRST;
    
    // Stats
    std::atomic<uint64_t> trades_entered_{0};
    std::atomic<uint64_t> trades_exited_{0};
    std::atomic<uint64_t> probe_failures_{0};
    std::atomic<uint64_t> probe_confirms_{0};
    uint64_t winning_trades_ = 0;
    
    // Kill switch
    bool auto_disabled_ = false;
    const char* disable_reason_ = "";
    
    // Timing
    uint64_t last_trade_ts_ns_ = 0;
    static constexpr uint64_t COOLDOWN_NS = 300'000'000ULL;  // 300ms for metals
    
    // Debug
    uint64_t tick_count_ = 0;
    MetalTick last_tick_{};
};

// =============================================================================
// IMPLEMENTATION
// =============================================================================

inline MetalMicroScalpEngine::MetalMicroScalpEngine(const std::string& symbol)
    : symbol_(symbol)
    , symbol_type_(parseMetalSymbol(symbol.c_str()))
    , fee_config_(getMetalFeeConfig(symbol.c_str()))
    , probe_params_(getMetalProbeParams(symbol.c_str()))
    , entry_params_(getMetalEntryParams(symbol.c_str()))  // v4.9.6: Per-symbol entry
{
    printf("[METAL-MICROSCALP] Created %s with 3-STATE LIFECYCLE (PER-ENGINE GATES)\n", symbol_.c_str());
    printf("[METAL-MICROSCALP]   ENTRY: conf>=%.2f ofi>=%.2f edge>=%.2f stable=%s trend=%s compress=%s warmup=%u\n",
           entry_params_.min_confidence, entry_params_.min_ofi, entry_params_.min_edge_bps,
           entry_params_.allow_stable_regime ? "YES" : "NO",
           entry_params_.require_trend_confirm ? "YES" : "NO",
           entry_params_.require_spread_compress ? "YES" : "NO",
           entry_params_.warmup_ticks);
    printf("[METAL-MICROSCALP]   PROBE: min=%dms max=%dms edge_drop=%.2f spread_exp=%.2f vol_conf=%.2f\n",
           probe_params_.probe_min_ms, probe_params_.probe_max_ms,
           probe_params_.edge_drop_ratio, probe_params_.spread_expand_mult, probe_params_.vol_confirm_mult);
    printf("[METAL-MICROSCALP]   CONFIRM: tp_exp=%.2f min_hold=%dms max_hold=%dms\n",
           probe_params_.tp_expansion, probe_params_.min_hold_ms, probe_params_.max_hold_ms);
    printf("[METAL-MICROSCALP]   RISK: daily_cap=%.2fbps max_streak=%d\n",
           probe_params_.daily_loss_cap_bps, probe_params_.max_loss_streak);
}

inline void MetalMicroScalpEngine::onTick(const MetalTick& tick) {
    if (!enabled_) return;
    if (auto_disabled_) return;
    
    tick_count_++;
    last_tick_ = tick;
    
    // ═══════════════════════════════════════════════════════════════════════
    // SESSION GATE - First check (07:00-17:00 UTC)
    // ═══════════════════════════════════════════════════════════════════════
    if (!session_.isSessionActive()) {
        // Force exit if we have a position
        if (state_ != MicroState::FLAT) {
            exit(tick, ExitReason::SESSION_CLOSE);
        }
        return;
    }
    
    // Don't enter new trades near session end
    if (session_.isNearSessionEnd() && state_ == MicroState::FLAT) {
        return;
    }
    
    // Update trackers
    spread_tracker_.onTick(tick.spread_bps);
    trend_tracker_.onTick(tick.mid, tick.ts_ns);
    regime_detector_.onTick(tick.mid, tick.volatility, tick.ofi);
    
    // Update edge
    last_edge_bps_ = calcEdgeBps(tick);
    
    // Log every 500 ticks
    if (tick_count_ % 500 == 0) {
        printf("[METAL-MICROSCALP][%s] tick=%llu state=%s edge=%.2f spread=%.2f regime=%s session=%s\n",
               symbol_.c_str(), (unsigned long long)tick_count_,
               microStateStr(state_), last_edge_bps_, tick.spread_bps,
               metalRegimeStr(regime_detector_.regime()), session_.sessionStatus());
    }
    
    // STATE MACHINE
    switch (state_) {
        case MicroState::FLAT:
            handleFlat(tick);
            break;
        case MicroState::PROBE:
            handleProbe(tick);
            break;
        case MicroState::CONFIRM:
            handleConfirm(tick);
            break;
    }
}

// =============================================================================
// STATE: FLAT - Looking for entry
// =============================================================================
inline void MetalMicroScalpEngine::handleFlat(const MetalTick& tick) {
    if (pending_fill_) return;
    
    double edge = last_edge_bps_;
    
    if (checkEntryFilters(tick, edge)) {
        enter(tick, edge);
    }
}

// =============================================================================
// STATE: PROBE - Evaluating post-entry structure
// =============================================================================
inline void MetalMicroScalpEngine::handleProbe(const MetalTick& tick) {
    int age_ms = (tick.ts_ns - snapshot_.ts_ns) / 1'000'000;
    double pnl = calcPnlBps(tick);
    
    // ═══════════════════════════════════════════════════════════════════════
    // FAIL CONDITIONS
    // ═══════════════════════════════════════════════════════════════════════
    
    // Spread expansion
    if (tick.spread_bps > snapshot_.spread_bps * probe_params_.spread_expand_mult) {
        probe_failures_++;
        exit(tick, ExitReason::SPREAD_EXPAND);
        printf("[METAL-MICROSCALP][%s] PROBE_FAIL: spread %.2f > %.2f\n",
               symbol_.c_str(), tick.spread_bps, 
               snapshot_.spread_bps * probe_params_.spread_expand_mult);
        return;
    }
    
    // Edge decay
    if (last_edge_bps_ < snapshot_.edge_bps * probe_params_.edge_drop_ratio) {
        probe_failures_++;
        exit(tick, ExitReason::EDGE_DECAY);
        printf("[METAL-MICROSCALP][%s] PROBE_FAIL: edge %.2f < %.2f\n",
               symbol_.c_str(), last_edge_bps_,
               snapshot_.edge_bps * probe_params_.edge_drop_ratio);
        return;
    }
    
    // Silver-specific: OFI flip = immediate exit
    if (symbol_type_ == MetalSymbol::XAGUSD) {
        if ((entry_ofi_ > 0 && tick.ofi < -0.1) || (entry_ofi_ < 0 && tick.ofi > 0.1)) {
            probe_failures_++;
            exit(tick, ExitReason::OFI_FLIP);
            printf("[METAL-MICROSCALP][XAGUSD] PROBE_FAIL: OFI flip %.2f → %.2f\n",
                   entry_ofi_, tick.ofi);
            return;
        }
    }
    
    // Stop loss
    if (pnl <= -probe_params_.sl_bps) {
        probe_failures_++;
        exit(tick, ExitReason::STOP_LOSS);
        printf("[METAL-MICROSCALP][%s] PROBE_FAIL: SL hit %.2f bps\n", symbol_.c_str(), pnl);
        return;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // CONFIRM CONDITIONS
    // ═══════════════════════════════════════════════════════════════════════
    
    double fee_floor = totalCostBps(tick);
    
    if (age_ms >= probe_params_.probe_min_ms &&
        tick.volatility > snapshot_.vol * probe_params_.vol_confirm_mult &&
        pnl > fee_floor) {
        
        state_ = MicroState::CONFIRM;
        confirm_ts_ns_ = tick.ts_ns;
        probe_confirms_++;
        
        printf("[METAL-MICROSCALP][%s] PROBE_CONFIRM: age=%dms vol=%.2f>%.2f pnl=%.2f>%.2f\n",
               symbol_.c_str(), age_ms,
               tick.volatility, snapshot_.vol * probe_params_.vol_confirm_mult,
               pnl, fee_floor);
        return;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // TIMEOUT
    // ═══════════════════════════════════════════════════════════════════════
    
    if (age_ms > probe_params_.probe_max_ms) {
        probe_failures_++;
        exit(tick, ExitReason::PROBE_TIMEOUT);
        printf("[METAL-MICROSCALP][%s] PROBE_TIMEOUT: age=%dms > max=%dms\n",
               symbol_.c_str(), age_ms, probe_params_.probe_max_ms);
        return;
    }
}

// =============================================================================
// STATE: CONFIRM - Winner window
// =============================================================================
inline void MetalMicroScalpEngine::handleConfirm(const MetalTick& tick) {
    int total_age_ms = (tick.ts_ns - snapshot_.ts_ns) / 1'000'000;
    double pnl = calcPnlBps(tick);
    
    // ═══════════════════════════════════════════════════════════════════════
    // HARD SAFETY
    // ═══════════════════════════════════════════════════════════════════════
    
    if (tick.spread_bps > snapshot_.spread_bps * probe_params_.spread_expand_mult) {
        exit(tick, ExitReason::SPREAD_BREAK);
        printf("[METAL-MICROSCALP][%s] CONFIRM_EXIT: spread break %.2f\n", 
               symbol_.c_str(), tick.spread_bps);
        return;
    }
    
    // Silver: OFI flip even in CONFIRM
    if (symbol_type_ == MetalSymbol::XAGUSD) {
        if ((entry_ofi_ > 0 && tick.ofi < -0.2) || (entry_ofi_ < 0 && tick.ofi > 0.2)) {
            exit(tick, ExitReason::OFI_FLIP);
            printf("[METAL-MICROSCALP][XAGUSD] CONFIRM_EXIT: OFI flip\n");
            return;
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // DYNAMIC TP
    // ═══════════════════════════════════════════════════════════════════════
    
    double fee_floor = totalCostBps(tick);
    double vol_bonus = (tick.volatility > snapshot_.vol) 
                       ? probe_params_.base_tp_bps * probe_params_.tp_expansion 
                       : 0.0;
    double effective_tp = probe_params_.base_tp_bps + fee_floor + vol_bonus;
    
    // Take profit
    if (pnl >= effective_tp) {
        exit(tick, ExitReason::TAKE_PROFIT);
        printf("[METAL-MICROSCALP][%s] CONFIRM_TP: pnl=%.2f >= tp=%.2f\n",
               symbol_.c_str(), pnl, effective_tp);
        return;
    }
    
    // Stop loss
    if (pnl <= -probe_params_.sl_bps) {
        exit(tick, ExitReason::STOP_LOSS);
        printf("[METAL-MICROSCALP][%s] CONFIRM_SL: pnl=%.2f\n", symbol_.c_str(), pnl);
        return;
    }
    
    // Maximum hold
    if (total_age_ms >= probe_params_.max_hold_ms) {
        exit(tick, ExitReason::TIME_STOP);
        printf("[METAL-MICROSCALP][%s] CONFIRM_TIME: age=%dms pnl=%.2f\n",
               symbol_.c_str(), total_age_ms, pnl);
        return;
    }
}

// =============================================================================
// ENTRY FILTERS
// =============================================================================
inline bool MetalMicroScalpEngine::checkEntryFilters(const MetalTick& tick, double edge_bps) {
    uint64_t now = tick.ts_ns;
    
    // v4.9.9: Log block reason every 1000 ticks when FLAT
    bool should_log = (tick_count_ % 1000 == 0);
    const char* block_reason = nullptr;
    
    // ═══════════════════════════════════════════════════════════════════════
    // GATE 0: Per-engine warmup (v4.9.6 - uses entry_params_)
    // Gold: 500 ticks (needs spread medians)
    // Silver: 300 ticks (faster warmup)
    // ═══════════════════════════════════════════════════════════════════════
    if (tick_count_ < entry_params_.warmup_ticks) {
        block_reason = "WARMUP";
        if (should_log) printf("[METAL-FILTER][%s] BLOCKED: %s (%llu/%d)\n", 
                               symbol_.c_str(), block_reason, (unsigned long long)tick_count_, entry_params_.warmup_ticks);
        return false;
    }
    
    // Cooldown between trades
    if (last_trade_ts_ns_ > 0 && now - last_trade_ts_ns_ < COOLDOWN_NS) {
        block_reason = "COOLDOWN";
        if (should_log) printf("[METAL-FILTER][%s] BLOCKED: %s\n", symbol_.c_str(), block_reason);
        return false;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // GATE 1: Per-engine regime permission (v4.9.6)
    // Gold: allow_stable_regime = true → can trade in STABLE
    // Silver: allow_stable_regime = false → needs TREND
    // ═══════════════════════════════════════════════════════════════════════
    MetalRegime regime = regime_detector_.regime();
    if (regime == MetalRegime::STABLE && !entry_params_.allow_stable_regime) {
        block_reason = "STABLE_REGIME";
        if (should_log) printf("[METAL-FILTER][%s] BLOCKED: %s (needs TREND)\n", symbol_.c_str(), block_reason);
        return false;
    }
    if (regime == MetalRegime::SPIKE) {
        block_reason = "SPIKE_REGIME";
        if (should_log) printf("[METAL-FILTER][%s] BLOCKED: %s\n", symbol_.c_str(), block_reason);
        return false;  // Never trade spikes
    }
    if (regime == MetalRegime::DEAD) {
        block_reason = "DEAD_REGIME";
        if (should_log) printf("[METAL-FILTER][%s] BLOCKED: %s\n", symbol_.c_str(), block_reason);
        return false;  // Dead regime = no edge
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // GATE 2: Per-engine OFI threshold (v4.9.6)
    // Gold: 0.30 | Silver: 0.40
    // ═══════════════════════════════════════════════════════════════════════
    if (std::fabs(tick.ofi) < entry_params_.min_ofi) {
        block_reason = "LOW_OFI";
        if (should_log) printf("[METAL-FILTER][%s] BLOCKED: %s (%.2f < %.2f)\n", 
                               symbol_.c_str(), block_reason, std::fabs(tick.ofi), entry_params_.min_ofi);
        return false;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // GATE 3: Per-engine edge threshold (v4.9.6)
    // Gold: 0.45 bps | Silver: 0.70 bps
    // ═══════════════════════════════════════════════════════════════════════
    if (edge_bps < entry_params_.min_edge_bps) {
        block_reason = "LOW_EDGE";
        if (should_log) printf("[METAL-FILTER][%s] BLOCKED: %s (%.2f < %.2f)\n", 
                               symbol_.c_str(), block_reason, edge_bps, entry_params_.min_edge_bps);
        return false;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // GATE 4: Spread widening gate
    // ═══════════════════════════════════════════════════════════════════════
    double median = spread_tracker_.medianSpread();
    if (median > 0 && tick.spread_bps > median * 1.6) {
        block_reason = "WIDE_SPREAD";
        if (should_log) printf("[METAL-FILTER][%s] BLOCKED: %s (%.2f > %.2f)\n", 
                               symbol_.c_str(), block_reason, tick.spread_bps, median * 1.6);
        return false;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // GATE 5: Per-engine conditional gates (v4.9.6)
    // These are controlled by entry_params_ flags
    // ═══════════════════════════════════════════════════════════════════════
    
    // Trend confirmation (Gold: required, Silver: required)
    if (entry_params_.require_trend_confirm) {
        if (!trend_tracker_.agreesWithOFI(tick.ofi)) {
            block_reason = "TREND_DISAGREE";
            if (should_log) printf("[METAL-FILTER][%s] BLOCKED: %s\n", symbol_.c_str(), block_reason);
            return false;
        }
    }
    
    // Spread compression (Gold: not required, Silver: required)
    if (entry_params_.require_spread_compress) {
        if (!spread_tracker_.isCompressing()) {
            block_reason = "NO_SPREAD_COMPRESS";
            if (should_log) printf("[METAL-FILTER][%s] BLOCKED: %s\n", symbol_.c_str(), block_reason);
            return false;
        }
    }
    
    // v4.9.9: All gates passed!
    if (should_log) printf("[METAL-FILTER][%s] READY edge=%.2f ofi=%.2f regime=%s\n", 
                           symbol_.c_str(), edge_bps, tick.ofi, metalRegimeStr(regime));
    return true;
}

// =============================================================================
// EDGE CALCULATION (Symbol-specific)
// =============================================================================
inline double MetalMicroScalpEngine::calcEdgeBps(const MetalTick& tick) {
    // v4.9.6: Per-symbol OFI floor from entry_params_
    // Gold: 0.30 | Silver: 0.40
    double ofi_floor = entry_params_.min_ofi;
    
    double abs_ofi = std::fabs(tick.ofi);
    if (abs_ofi < ofi_floor) return 0.0;
    
    // ═══════════════════════════════════════════════════════════════════════
    // EDGE = EXPECTED PROFIT (not signal strength)
    // ═══════════════════════════════════════════════════════════════════════
    // Metals via CFD have lower fees (spread only, no commission typically)
    // But we still need edge > cost to be profitable
    //
    // Gold: Typical spread ~1-2 bps, cost ~0.5-1 bps
    //       Strong OFI (0.6+) should yield ~2-4 bps expected profit
    // Silver: Typical spread ~2-4 bps, cost ~1-2 bps
    //         Strong OFI (0.7+) should yield ~3-6 bps expected profit
    // ═══════════════════════════════════════════════════════════════════════
    
    double edge = 0.0;
    
    if (symbol_type_ == MetalSymbol::XAUUSD) {
        // Gold: Patient, needs trend confirmation
        // OFI contribution: multiplier 5.0 so OFI 0.6 → 1.5 bps base
        double ofi_edge = (abs_ofi - ofi_floor) * 5.0;
        
        // Pressure alignment bonus
        double pressure_bonus = 0.0;
        if ((tick.ofi > 0.4 && tick.pressure > 0.3) || 
            (tick.ofi < -0.4 && tick.pressure < -0.3)) {
            pressure_bonus = 0.8;
        }
        
        // Trend bias bonus (gold trends, use it)
        double trend_bias = trend_tracker_.trendBias();
        double trend_bonus = std::fabs(trend_bias) * 1.5;
        
        // Direction alignment: OFI and trend agree
        bool trend_aligned = (tick.ofi > 0 && trend_bias > 0.1) || 
                            (tick.ofi < 0 && trend_bias < -0.1);
        if (trend_aligned) {
            trend_bonus *= 1.5;
        }
        
        edge = ofi_edge + pressure_bonus + trend_bonus;
        edge = std::min(edge, 8.0);  // Cap at 8 bps
    }
    else if (symbol_type_ == MetalSymbol::XAGUSD) {
        // Silver: Opportunistic, needs spread compression
        // OFI contribution: multiplier 6.0 so OFI 0.7 → 1.8 bps base
        double ofi_edge = (abs_ofi - ofi_floor) * 6.0;
        
        // Pressure alignment bonus (silver needs strong alignment)
        double pressure_bonus = 0.0;
        if ((tick.ofi > 0.5 && tick.pressure > 0.4) || 
            (tick.ofi < -0.5 && tick.pressure < -0.4)) {
            pressure_bonus = 1.2;
        }
        
        // Spread compression bonus (key for silver)
        double compression_bonus = spread_tracker_.isCompressing() ? 1.5 : 0.0;
        
        edge = ofi_edge + pressure_bonus + compression_bonus;
        edge = std::min(edge, 10.0);  // Cap at 10 bps
    }
    else {
        double ofi_edge = (abs_ofi - ofi_floor) * 5.0;
        edge = ofi_edge;
    }
    
    return edge;
}

// =============================================================================
// ENTRY
// =============================================================================
inline void MetalMicroScalpEngine::enter(const MetalTick& tick, double edge_bps) {
    double qty = base_qty_;
    
    if (qty <= 0.0) return;
    
    // Create entry snapshot
    snapshot_.price = tick.mid;
    snapshot_.edge_bps = edge_bps;
    snapshot_.spread_bps = tick.spread_bps;
    snapshot_.vol = tick.volatility;
    snapshot_.ofi = tick.ofi;
    snapshot_.ts_ns = tick.ts_ns;
    snapshot_.fill_type = FillType::UNKNOWN;
    
    entry_ofi_ = tick.ofi;  // Store for OFI flip detection
    long_side_ = (tick.ofi > 0);
    
    // Send order
    double limit_price = (routing_mode_ == RoutingMode::MAKER_FIRST) 
                         ? (long_side_ ? tick.bid : tick.ask) 
                         : 0.0;
    if (order_cb_) {
        order_cb_(symbol_.c_str(), long_side_, qty, limit_price, routing_mode_);
    }
    
    if (routing_mode_ == RoutingMode::MAKER_FIRST) {
        pending_fill_ = true;
    } else {
        entry_fill_type_ = FillType::TAKER;
        snapshot_.fill_type = FillType::TAKER;
        state_ = MicroState::PROBE;
    }
    
    last_trade_ts_ns_ = tick.ts_ns;
    trades_entered_++;
    
    if (trade_cb_) {
        trade_cb_(symbol_.c_str(), long_side_ ? +1 : -1, qty, tick.mid, 0.0);
    }
    
    printf("[METAL-MICROSCALP][%s] ENTER edge=%.2f spread=%.2f qty=%.4f side=%s → PROBE\n",
           symbol_.c_str(), edge_bps, tick.spread_bps, qty, long_side_ ? "LONG" : "SHORT");
}

// =============================================================================
// EXIT
// =============================================================================
inline void MetalMicroScalpEngine::exit(const MetalTick& tick, ExitReason reason) {
    double qty = base_qty_;
    double exit_price = long_side_ ? tick.bid : tick.ask;
    
    if (qty <= 0.0) {
        state_ = MicroState::FLAT;
        return;
    }
    
    // Send exit order
    if (order_cb_) {
        order_cb_(symbol_.c_str(), !long_side_, qty, 0.0, RoutingMode::TAKER_ONLY);
    }
    exit_fill_type_ = FillType::TAKER;
    
    // Compute PnL
    MetalPnLAttribution attr;
    double signed_pnl = long_side_ 
                        ? (exit_price - snapshot_.price) / snapshot_.price * 10000.0
                        : (snapshot_.price - exit_price) / snapshot_.price * 10000.0;
    attr.raw_pnl_bps = signed_pnl;
    attr.spread_cost = snapshot_.spread_bps * fee_config_.spread_cost_mult;
    attr.net_pnl_bps = signed_pnl - attr.spread_cost;
    attr.entry_fill = snapshot_.fill_type;
    attr.exit_fill = exit_fill_type_;
    attr.log(symbol_.c_str(), reason);
    
    trades_exited_++;
    bool is_loss = attr.net_pnl_bps < 0;
    
    if (!is_loss) {
        winning_trades_++;
    }
    
    // Update loss tracker and check kill conditions
    loss_tracker_.recordTrade(is_loss, attr.net_pnl_bps, tick.ts_ns);
    checkKillConditions(attr.net_pnl_bps);
    
    if (trade_cb_) {
        trade_cb_(symbol_.c_str(), long_side_ ? -1 : +1, qty, exit_price, attr.net_pnl_bps);
    }
    
    // Stats
    int total_age_ms = (tick.ts_ns - snapshot_.ts_ns) / 1'000'000;
    printf("[METAL-MICROSCALP][%s] EXIT @ %.5f net=%.2fbps age=%dms state=%s winrate=%.1f%% streak=%d\n",
           symbol_.c_str(), exit_price, attr.net_pnl_bps, total_age_ms,
           microStateStr(state_), winRate() * 100.0, loss_tracker_.lossStreak());
    
    // Return to FLAT
    state_ = MicroState::FLAT;
    last_trade_ts_ns_ = tick.ts_ns;
}

// =============================================================================
// FILL HANDLING
// =============================================================================
inline void MetalMicroScalpEngine::onFill(FillType fill_type, double filled_price) {
    if (pending_fill_) {
        entry_fill_type_ = fill_type;
        snapshot_.fill_type = fill_type;
        snapshot_.price = filled_price;
        pending_fill_ = false;
        state_ = MicroState::PROBE;
        
        printf("[METAL-MICROSCALP][%s] FILL: %s @ %.5f → PROBE\n",
               symbol_.c_str(), fillTypeStr(fill_type), filled_price);
    }
}

inline void MetalMicroScalpEngine::onMakerTimeout() {
    if (pending_fill_) {
        double edge = last_edge_bps_;
        double taker_cost = totalCostBps(last_tick_) * 1.5;  // Assume worse for taker
        
        if (edge < taker_cost + 0.2) {
            pending_fill_ = false;
            state_ = MicroState::FLAT;
            printf("[METAL-MICROSCALP][%s] MAKER_ABORT: edge=%.2f < cost=%.2f\n",
                   symbol_.c_str(), edge, taker_cost);
            return;
        }
        
        printf("[METAL-MICROSCALP][%s] MAKER_TIMEOUT: taker fallback\n", symbol_.c_str());
    }
}

// =============================================================================
// KILL SWITCH
// =============================================================================
inline void MetalMicroScalpEngine::checkKillConditions(double pnl_bps) {
    // Loss streak check
    if (loss_tracker_.lossStreak() >= probe_params_.max_loss_streak) {
        auto_disabled_ = true;
        disable_reason_ = "LOSS_STREAK";
        printf("[METAL-MICROSCALP-KILL] symbol=%s reason=%s loss_streak=%d pnl=%.2fbps\n",
               symbol_.c_str(), disable_reason_, loss_tracker_.lossStreak(), pnl_bps);
        return;
    }
    
    // Daily loss cap
    if (loss_tracker_.dailyPnlBps() <= probe_params_.daily_loss_cap_bps) {
        auto_disabled_ = true;
        disable_reason_ = "DAILY_CAP";
        printf("[METAL-MICROSCALP-KILL] symbol=%s reason=%s daily_pnl=%.2fbps cap=%.2fbps\n",
               symbol_.c_str(), disable_reason_, 
               loss_tracker_.dailyPnlBps(), probe_params_.daily_loss_cap_bps);
        return;
    }
}

// =============================================================================
// CALCULATIONS
// =============================================================================
inline double MetalMicroScalpEngine::calcPnlBps(const MetalTick& tick) const {
    if (snapshot_.price <= 0.0) return 0.0;
    double exit_price = long_side_ ? tick.bid : tick.ask;
    return long_side_ 
           ? (exit_price - snapshot_.price) / snapshot_.price * 10000.0
           : (snapshot_.price - exit_price) / snapshot_.price * 10000.0;
}

inline double MetalMicroScalpEngine::totalCostBps(const MetalTick& tick) const {
    return tick.spread_bps * fee_config_.spread_cost_mult + fee_config_.commission_bps;
}

} // namespace Metal
} // namespace Chimera
