#pragma once
// =============================================================================
// CryptoMicroScalp.hpp - Speed-First Crypto Micro-Scalping Engine
// =============================================================================
// v4.9.25: FEEDBACK LOOP ELIMINATION
//
// THE 5 CRITICAL FIXES (from analysis document):
//
// FIX #1: HARD GATE on ExecutionGovernor state
//   - Check canSubmit() at TOP of checkEntryFilters
//   - If venue is HALTED/RECOVERY: DO NOT ENTER, DO NOT PROBE, DO NOT LOG PnL
//   - This prevents: Strategy fires → Orders blocked → Fake losses → Alarm spam
//
// FIX #2: PROBE_TIMEOUT ≠ LOSS when venue unhealthy
//   - A timeout during venue halt is NOT a trade, NOT a loss, NOT expectancy data
//   - Don't poison statistics with connectivity-induced "failures"
//
// FIX #3: Alarm latches (implemented in ExecutionGovernor)
//   - State TRANSITIONS fire alarms
//   - State PERSISTENCE does not
//   - 100 alarms → 2 alarms
//
// FIX #4: Probe backoff FREEZES symbol
//   - Consecutive timeouts → symbol enters FROZEN state
//   - Timer-based resume only
//   - Prevents rapid-fire probe spam
//
// FIX #5: Venue HALT does NOT degrade alpha
//   - Don't update governor heat during connectivity issues
//   - Don't reduce confidence
//   - Don't record fake losses to expectancy
//   - Only execution health changes, not alpha health
//
// v4.9.8 LEGACY:
//   - calcEdgeBps() → calcGrossEdgeBps() - NO FEES in entry gate
//   - Exit validation → must clear fees (net > 0)
//   - CryptoSafetyGovernor integration (auto-relax/tighten)
//   - HYBRID routing mode: maker-first, taker on high edge
//   - Dynamic maker timeout based on fill rate
//   - FeeLossGuard + TakerContaminationGuard
//
// ROUTING MODES:
//   BTC: TAKER_ONLY (non-negotiable - maker orders don't fill)
//   ETH: HYBRID (maker-first, taker on edge > 1.6 bps)
//   SOL: HYBRID (maker-first, taker on edge > 2.8 bps)
// =============================================================================

#include <string>
#include <cstdint>
#include <chrono>
#include <functional>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>

// v4.9.8: Safety governors
#include "../safety/CryptoSafetyGovernor.hpp"
#include "../safety/FeeLossGuard.hpp"
#include "../safety/GovernorPersistence.hpp"

// v4.9.10: Latency-aware trading
#include "latency/HotPathLatencyTracker.hpp"
#include "latency/LatencyGate.hpp"

// v4.9.25: ExecutionGovernor for venue state gating (FIX #1)
#include "execution/ExecutionGovernor.hpp"

#include <cassert>

namespace Chimera {
namespace Crypto {

// =============================================================================
// THE 3-STATE LIFECYCLE
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
    MAKER_ONLY  = 2,
    HYBRID      = 3   // v4.9.8: maker-first, taker on high edge
};

inline const char* routingModeStr(RoutingMode m) {
    switch (m) {
        case RoutingMode::TAKER_ONLY:  return "TAKER_ONLY";
        case RoutingMode::MAKER_FIRST: return "MAKER_FIRST";
        case RoutingMode::MAKER_ONLY:  return "MAKER_ONLY";
        case RoutingMode::HYBRID:      return "HYBRID";
    }
    return "UNK";
}

// =============================================================================
// Fee Configuration
// =============================================================================
struct FeeConfig {
    double maker_fee_bps = 0.2;
    double taker_fee_bps = 4.0;
    uint32_t maker_timeout_ms = 40;
    
    // v4.9.8: HYBRID mode thresholds
    double hybrid_taker_edge_bps = 1.6;  // Min edge to allow taker
};

inline FeeConfig getFeeConfig(const char* symbol) {
    FeeConfig cfg;
    if (strcmp(symbol, "BTCUSDT") == 0) {
        cfg.maker_fee_bps = 0.2;
        cfg.taker_fee_bps = 4.0;
        cfg.maker_timeout_ms = 220;      // v4.9.8: From spec
        cfg.hybrid_taker_edge_bps = 99.0; // Never allow taker for BTC
    } else if (strcmp(symbol, "ETHUSDT") == 0) {
        cfg.maker_fee_bps = 0.2;
        cfg.taker_fee_bps = 4.0;
        cfg.maker_timeout_ms = 160;
        cfg.hybrid_taker_edge_bps = 1.6;  // Allow taker if edge > 1.6 bps
    } else if (strcmp(symbol, "SOLUSDT") == 0) {
        cfg.maker_fee_bps = 0.2;
        cfg.taker_fee_bps = 4.0;
        cfg.maker_timeout_ms = 100;
        cfg.hybrid_taker_edge_bps = 2.8;  // Allow taker if edge > 2.8 bps
    } else {
        cfg.maker_fee_bps = 0.2;
        cfg.taker_fee_bps = 4.0;
        cfg.maker_timeout_ms = 150;
        cfg.hybrid_taker_edge_bps = 2.0;
    }
    return cfg;
}

// =============================================================================
// SYMBOL-SPECIFIC PROBE PARAMETERS (from CSV analysis)
// =============================================================================
struct ProbeParams {
    int probe_min_ms;       // Minimum time in PROBE before can CONFIRM
    int probe_max_ms;       // Maximum time in PROBE before timeout
    double edge_drop_ratio; // If edge drops below this * entry_edge, FAIL
    double spread_expand;   // If spread > this * entry_spread, FAIL
    double vol_confirm;     // Vol must be > this * entry_vol to CONFIRM
    double tp_expansion;    // TP multiplier in CONFIRM mode
    int confirm_min_hold_ms;// Minimum hold time in CONFIRM
    int confirm_max_hold_ms;// Maximum hold time in CONFIRM
};

inline ProbeParams getProbeParams(const char* symbol) {
    ProbeParams p;
    
    if (strcmp(symbol, "BTCUSDT") == 0) {
        // BTC: Fast, disciplined - winners show up quickly
        p.probe_min_ms = 30;
        p.probe_max_ms = 90;
        p.edge_drop_ratio = 0.6;
        p.spread_expand = 1.25;
        p.vol_confirm = 1.35;
        p.tp_expansion = 0.4;
        p.confirm_min_hold_ms = 120;
        p.confirm_max_hold_ms = 400;
    } 
    else if (strcmp(symbol, "ETHUSDT") == 0) {
        // ETH: Patient, selective - continuation is delayed
        p.probe_min_ms = 60;
        p.probe_max_ms = 160;
        p.edge_drop_ratio = 0.5;
        p.spread_expand = 1.35;
        p.vol_confirm = 1.50;
        p.tp_expansion = 0.7;
        p.confirm_min_hold_ms = 180;
        p.confirm_max_hold_ms = 650;
    } 
    else if (strcmp(symbol, "SOLUSDT") == 0) {
        // SOL: Strict, defensive - only real moves survive
        p.probe_min_ms = 40;
        p.probe_max_ms = 100;
        p.edge_drop_ratio = 0.7;
        p.spread_expand = 1.20;
        p.vol_confirm = 1.70;
        p.tp_expansion = 1.0;  // Aggressive expansion for SOL winners
        p.confirm_min_hold_ms = 200;
        p.confirm_max_hold_ms = 900;
    } 
    else {
        // Default
        p.probe_min_ms = 50;
        p.probe_max_ms = 120;
        p.edge_drop_ratio = 0.6;
        p.spread_expand = 1.30;
        p.vol_confirm = 1.50;
        p.tp_expansion = 0.5;
        p.confirm_min_hold_ms = 150;
        p.confirm_max_hold_ms = 500;
    }
    
    return p;
}

// =============================================================================
// SYMBOL-SPECIFIC ENTRY PARAMETERS (v4.9.8: FEE-AGNOSTIC)
// =============================================================================
struct EntryParams {
    double min_confidence;       // Minimum regime confidence to trade
    double min_ofi;              // Minimum |OFI| for entry
    double min_gross_edge_bps;   // v4.9.8: GROSS edge minimum (NO FEES)
    bool allow_stable_regime;    // Can trade when regime == STABLE?
    uint32_t warmup_ticks;       // Per-engine warmup
    
    // v4.9.8: TP/SL targets (in bps)
    double tp_bps;
    double sl_bps;
};

inline EntryParams getEntryParams(const char* symbol) {
    EntryParams p;
    
    if (strcmp(symbol, "BTCUSDT") == 0) {
        // BTC: TRADES IN STABLE/RANGE - pure scalping
        // MAKER_ONLY means we need less gross edge
        p.min_confidence = 0.55;        // v4.9.8: Slightly higher (governor can relax)
        p.min_ofi = 0.20;
        p.min_gross_edge_bps = 0.8;     // v4.9.8: GROSS edge only, no fee subtraction
        p.allow_stable_regime = true;
        p.warmup_ticks = 100;
        p.tp_bps = 2.2;                 // From spec
        p.sl_bps = 1.6;
    } 
    else if (strcmp(symbol, "ETHUSDT") == 0) {
        // ETH: Needs more conviction, slower moves
        p.min_confidence = 0.58;
        p.min_ofi = 0.30;
        p.min_gross_edge_bps = 1.2;     // Higher because HYBRID may use taker
        p.allow_stable_regime = true;
        p.warmup_ticks = 200;
        p.tp_bps = 2.6;
        p.sl_bps = 1.9;
    } 
    else if (strcmp(symbol, "SOLUSDT") == 0) {
        // SOL: Strictest - only impulse/continuation
        p.min_confidence = 0.56;
        p.min_ofi = 0.35;
        p.min_gross_edge_bps = 1.8;     // Highest because most volatile
        p.allow_stable_regime = false;
        p.warmup_ticks = 300;
        p.tp_bps = 3.2;
        p.sl_bps = 2.2;
    } 
    else {
        // Default: Conservative
        p.min_confidence = 0.60;
        p.min_ofi = 0.30;
        p.min_gross_edge_bps = 1.0;
        p.allow_stable_regime = true;
        p.warmup_ticks = 200;
        p.tp_bps = 2.0;
        p.sl_bps = 1.5;
    }
    
    return p;
}

// =============================================================================
// Entry Snapshot
// =============================================================================
struct EntrySnapshot {
    double price;
    double gross_edge_bps;   // v4.9.8: GROSS edge at entry
    double spread_bps;
    double vol;
    double ofi;
    uint64_t ts_ns;
    FillType fill_type;
    double qty;              // v4.9.9: Store qty for fill callback
};

// =============================================================================
// Tick data
// =============================================================================
struct MicroTick {
    double bid;
    double ask;
    double mid;
    double ofi;
    double pressure;
    double volatility;
    double latency_ms;
    uint64_t ts_ns;
};

// =============================================================================
// Callbacks
// =============================================================================
using MicroTradeCallback = std::function<void(
    const char* symbol, int8_t side, double qty, double price, double pnl_bps
)>;

using MicroOrderCallback = std::function<void(
    const char* symbol, bool is_buy, double qty, double price, RoutingMode routing
)>;

using FillNotifyCallback = std::function<void(
    const char* symbol, FillType fill_type, double filled_price
)>;

// =============================================================================
// Spread Tracker
// =============================================================================
class SpreadTracker {
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
    bool isAnomaly(double spread_bps) const {
        return median_spread_ > 0 && spread_bps > median_spread_ * 1.3;
    }
    
private:
    static constexpr size_t WINDOW_SIZE = 100;
    static constexpr size_t MIN_SAMPLES = 20;
    std::deque<double> spreads_;
    double median_spread_ = 0.0;
};

// =============================================================================
// Loss Clustering Tracker
// =============================================================================
class LossClusterTracker {
public:
    void recordTrade(bool is_loss, uint64_t ts_ns) {
        trades_.push_back({is_loss, ts_ns});
        while (!trades_.empty() && ts_ns - trades_.front().ts_ns > WINDOW_NS) {
            trades_.pop_front();
        }
    }
    
    int recentLosses() const {
        int count = 0;
        for (const auto& t : trades_) if (t.is_loss) count++;
        return count;
    }
    
    bool shouldPause() const { return recentLosses() >= 2; }
    void reset() { trades_.clear(); }
    
private:
    static constexpr uint64_t WINDOW_NS = 5ULL * 60 * 1'000'000'000ULL;
    struct TradeRecord { bool is_loss; uint64_t ts_ns; };
    std::deque<TradeRecord> trades_;
};

// =============================================================================
// Regime Detector
// =============================================================================
enum class MicroRegime : uint8_t { STABLE = 0, BURST = 1, VOLATILE = 2 };

inline const char* regimeStr(MicroRegime r) {
    switch (r) {
        case MicroRegime::STABLE:   return "STABLE";
        case MicroRegime::BURST:    return "BURST";
        case MicroRegime::VOLATILE: return "VOLATILE";
    }
    return "UNK";
}

class MicroRegimeDetector {
public:
    void onTick(double mid, double volatility, double ofi) {
        if (last_mid_ > 0.0) {
            double move = std::fabs(mid - last_mid_) / last_mid_;
            ema_move_ = 0.05 * move + 0.95 * ema_move_;
            bool up = (mid > last_mid_);
            if (up == last_dir_) dir_streak_++;
            else dir_streak_ = 1;
            last_dir_ = up;
        }
        last_mid_ = mid;
        last_vol_ = volatility;
        last_ofi_ = ofi;
        updateConfidence();
    }
    
    MicroRegime regime() const {
        if (ema_move_ < 0.00003) return MicroRegime::STABLE;
        if (ema_move_ < 0.00012) return MicroRegime::BURST;
        return MicroRegime::VOLATILE;
    }
    
    double confidence() const { return confidence_; }
    double emaMove() const { return ema_move_; }
    
private:
    void updateConfidence() {
        double streak_score = std::min(1.0, dir_streak_ / 5.0);
        double ofi_score = std::min(1.0, std::fabs(last_ofi_));
        MicroRegime r = regime();
        double vol_score = 1.0;
        if (r == MicroRegime::STABLE && last_vol_ > 1.0) vol_score = 0.5;
        if (r == MicroRegime::BURST && last_vol_ < 0.3) vol_score = 0.5;
        confidence_ = (streak_score * 0.4 + ofi_score * 0.4 + vol_score * 0.2);
    }
    
    double last_mid_ = 0.0, ema_move_ = 0.0, last_vol_ = 0.0, last_ofi_ = 0.0;
    double confidence_ = 0.0;
    int dir_streak_ = 0;
    bool last_dir_ = true;
};

// =============================================================================
// SOL Regime Detector
// =============================================================================
enum class SolRegime : uint8_t { DEAD = 0, COMPRESSING = 1, IMPULSE = 2, CONTINUATION = 3 };

inline const char* solRegimeStr(SolRegime r) {
    switch (r) {
        case SolRegime::DEAD:        return "DEAD";
        case SolRegime::COMPRESSING: return "COMPRESS";
        case SolRegime::IMPULSE:     return "IMPULSE";
        case SolRegime::CONTINUATION:return "CONT";
    }
    return "UNK";
}

class SolRegimeDetector {
public:
    void onTick(double volatility, double edge_bps, uint64_t ts_ns) {
        if (volatility < 0.5) { regime_ = SolRegime::COMPRESSING; return; }
        if (edge_bps > 0.9 && volatility > 0.9) {
            regime_ = SolRegime::IMPULSE;
            impulse_ts_ = ts_ns;
            return;
        }
        if (impulse_ts_ > 0 && ts_ns - impulse_ts_ < 250'000'000ULL) {
            regime_ = SolRegime::CONTINUATION;
            return;
        }
        regime_ = SolRegime::DEAD;
    }
    
    SolRegime regime() const { return regime_; }
    bool canTrade() const { return regime_ == SolRegime::IMPULSE || regime_ == SolRegime::CONTINUATION; }
    
private:
    SolRegime regime_ = SolRegime::DEAD;
    uint64_t impulse_ts_ = 0;
};

// =============================================================================
// Symbol Type
// =============================================================================
enum class CryptoSymbol : uint8_t { BTCUSDT = 0, ETHUSDT = 1, SOLUSDT = 2, OTHER = 255 };

inline CryptoSymbol parseSymbol(const char* sym) {
    if (strcmp(sym, "BTCUSDT") == 0) return CryptoSymbol::BTCUSDT;
    if (strcmp(sym, "ETHUSDT") == 0) return CryptoSymbol::ETHUSDT;
    if (strcmp(sym, "SOLUSDT") == 0) return CryptoSymbol::SOLUSDT;
    return CryptoSymbol::OTHER;
}

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
        default: return "UNK";
    }
}

// =============================================================================
// PnL Attribution (v4.9.8: Tracks gross vs net)
// =============================================================================
struct PnLAttribution {
    double gross_pnl_bps;    // v4.9.8: Before fees
    double spread_cost;
    double fee_cost;
    double net_pnl_bps;      // After fees
    FillType entry_fill;
    FillType exit_fill;
    
    void compute(double entry_price, double exit_price, double entry_spread_bps,
                 FillType entry_type, FillType exit_type, const FeeConfig& fees) {
        gross_pnl_bps = (exit_price - entry_price) / entry_price * 10000.0;
        spread_cost = entry_spread_bps * 0.5;
        double entry_fee = (entry_type == FillType::MAKER) ? fees.maker_fee_bps : fees.taker_fee_bps;
        double exit_fee = (exit_type == FillType::MAKER) ? fees.maker_fee_bps : fees.taker_fee_bps;
        fee_cost = entry_fee + exit_fee;
        net_pnl_bps = gross_pnl_bps - spread_cost - fee_cost;
        entry_fill = entry_type;
        exit_fill = exit_type;
    }
    
    bool isFeeOnlyLoss() const {
        return gross_pnl_bps > 0 && net_pnl_bps < 0;
    }
    
    void log(const char* symbol, ExitReason reason) const {
        printf("[MICROSCALP-PNL] %s gross=%.2f spread=-%.2f fee=-%.2f net=%.2f fills=%s/%s reason=%s%s\n",
               symbol, gross_pnl_bps, spread_cost, fee_cost, net_pnl_bps,
               fillTypeStr(entry_fill), fillTypeStr(exit_fill), exitReasonStr(reason),
               isFeeOnlyLoss() ? " [FEE-ONLY-LOSS]" : "");
    }
};

// =============================================================================
// CryptoMicroScalp Engine - THE CORE
// =============================================================================
class CryptoMicroScalpEngine {
public:
    explicit CryptoMicroScalpEngine(const std::string& symbol);
    
    void onTick(const MicroTick& tick);
    void onFill(FillType fill_type, double filled_price);
    void onMakerTimeout();
    
    // Callbacks
    void setOrderCallback(MicroOrderCallback cb) { order_cb_ = std::move(cb); }
    void setTradeCallback(MicroTradeCallback cb) { trade_cb_ = std::move(cb); }
    void setFillNotifyCallback(FillNotifyCallback cb) { fill_notify_cb_ = std::move(cb); }
    
    // v4.9.10: Latency snapshot callback for hot-path latency gating
    using LatencySnapshotCallback = std::function<Chimera::HotPathLatencyTracker::LatencySnapshot()>;
    void setLatencySnapshotCallback(LatencySnapshotCallback cb) { latency_snapshot_cb_ = std::move(cb); }
    
    // v4.9.10: Configure latency gate thresholds
    void setLatencyGateConfig(const Chimera::LatencyGateConfig& cfg) { latency_gate_.setConfig(cfg); }
    
    // v4.9.10: Get last latency gate result (for dashboard)
    const Chimera::LatencyGateResult& lastLatencyGateResult() const { return last_latency_result_; }
    Chimera::HotPathState lastLatencyState() const { return last_latency_result_.state; }
    Chimera::ExecMode lastExecMode() const { return last_latency_result_.exec_mode; }
    double lastRequiredEdgeBps() const { return last_latency_result_.required_edge_bps; }
    
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
    double totalPnlBps() const { return total_pnl_bps_; }
    double winRate() const { 
        return trades_exited_ > 0 ? (double)winning_trades_ / trades_exited_ : 0.0; 
    }
    
    // v4.9.10: Current mid price for bootstrap probes
    double currentMid() const { return last_mid_; }
    
    // v4.9.8: Get reason why not trading (for dashboard)
    const char* getBlockReason() const { return last_block_reason_; }
    
    // Regime
    MicroRegime currentRegime() const { return regime_detector_.regime(); }
    double regimeConfidence() const { return regime_detector_.confidence(); }
    SolRegime currentSolRegime() const { return sol_regime_detector_.regime(); }
    
    // v4.9.8: Governor state
    GovernorState governorState() const { return governor_telem_.state; }
    const GovernorTelemetry& governorTelemetry() const { return governor_telem_; }
    
    // v4.9.8: Governor heat (one number for dashboard)
    double governorHeat() const { return governor_heat_; }
    double sizeMultiplierFromHeat() const {
        return computeFinalSizeMultiplier(governor_heat_, size_config_);
    }
    
    // v4.9.8: Decision trace for debugging
    DecisionTrace getDecisionTrace(bool profile_ok = true, bool exec_ok = true,
                                    bool ml_ok = true, bool gonogo_ok = true) const {
        return DecisionTrace{
            symbol_.c_str(),
            governor_telem_.state,
            governor_heat_,
            sizeMultiplierFromHeat(),
            profile_ok, exec_ok, ml_ok, gonogo_ok,
            entry_params_.min_confidence,
            entry_params_.min_gross_edge_bps,
            gov_params_.confirmation_ticks,
            gov_params_.maker_timeout_ms
        };
    }
    
    // v4.9.8: Fee loss tracking
    int feeOnlyLosses() const { return fee_loss_guard_.feeOnlyLosses(); }
    double takerRatio() const { return taker_guard_.takerRatio(); }
    
    // v4.9.8: State persistence (survives restarts)
    void saveState() const {
        PersistedGovernorState ps = captureState(gov_params_, 
                                                  governor_telem_.state,
                                                  governor_.relaxSteps(),
                                                  governor_heat_);
        saveGovernorState(symbol_.c_str(), ps);
    }
    
    bool loadState() {
        PersistedGovernorState ps;
        if (loadGovernorState(symbol_.c_str(), ps)) {
            applyRestoredState(gov_params_, ps, governor_.bounds());
            governor_heat_ = ps.governor_heat;
            // Apply to entry params
            entry_params_.min_confidence = gov_params_.entry_confidence_min;
            entry_params_.min_gross_edge_bps = gov_params_.expectancy_min_bps;
            fee_config_.maker_timeout_ms = gov_params_.maker_timeout_ms;
            return true;
        }
        return false;
    }
    
    // SOL state
    bool isSolAutoDisabled() const { return sol_auto_disabled_; }
    void resetSolSession() {
        sol_loss_streak_ = 0;
        sol_daily_pnl_bps_ = 0.0;
        sol_auto_disabled_ = false;
        loss_cluster_.reset();
    }
    
    static constexpr uint8_t ENGINE_ID = 0;
    static constexpr uint8_t STRATEGY_ID = 3;

private:
    // State machine handlers
    void handleFlat(const MicroTick& tick);
    void handleProbe(const MicroTick& tick);
    void handleConfirm(const MicroTick& tick);
    
    // Entry/Exit
    bool checkEntryFilters(const MicroTick& tick, double gross_edge_bps);
    void enter(const MicroTick& tick, double gross_edge_bps);
    void exit(const MicroTick& tick, ExitReason reason);
    
    // v4.9.8: GROSS edge calculation (NO FEES)
    double calcGrossEdgeBps(const MicroTick& tick) const;
    
    // Fee calculations (for exit only)
    double calcNetPnlBps(const MicroTick& tick) const;
    double totalFeeCostBps() const;
    double spreadCostBps(const MicroTick& tick) const;
    
    // Thresholds
    double effectiveTP(const MicroTick& tick) const;
    double stopLossBps() const;
    double sizeMultiplier() const;
    double latencyThresholdMs() const;
    
    // v4.9.8: Determine effective routing for this trade
    RoutingMode effectiveRouting(double gross_edge_bps) const;
    
    static inline uint64_t nowNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }

private:
    std::string symbol_;
    CryptoSymbol symbol_type_;
    MicroRegimeDetector regime_detector_;
    SolRegimeDetector sol_regime_detector_;
    SpreadTracker spread_tracker_;
    LossClusterTracker loss_cluster_;
    FeeConfig fee_config_;
    ProbeParams probe_params_;
    EntryParams entry_params_;
    
    // v4.9.8: Safety governors
    CryptoSafetyGovernor governor_;
    GovernorTelemetry governor_telem_;
    SymbolParams gov_params_;
    SymbolHealth health_;
    
    // v4.9.8: Governor heat & size scaling
    double governor_heat_ = 0.0;
    SizeScalerConfig size_config_;
    
    // v4.9.8: Fee loss guards
    FeeLossGuard fee_loss_guard_;
    TakerContaminationGuard taker_guard_;
    DynamicMakerTimeout maker_timeout_;
    
    // STATE MACHINE
    MicroState state_ = MicroState::FLAT;
    EntrySnapshot snapshot_{};
    uint64_t confirm_ts_ns_ = 0;
    double last_gross_edge_bps_ = 0.0;
    char last_block_reason_[64] = "STARTUP";  // v4.9.12: Increased from 32 to fix truncation warning
    
    // Position
    bool long_side_ = true;
    FillType entry_fill_type_ = FillType::UNKNOWN;
    FillType exit_fill_type_ = FillType::UNKNOWN;
    bool pending_fill_ = false;
    
    // Callbacks
    MicroOrderCallback order_cb_;
    
    // v4.9.10: Latency gate
    LatencySnapshotCallback latency_snapshot_cb_;
    Chimera::LatencyGate latency_gate_{Chimera::latencyConfigScalp()};  // Use scalp preset
    Chimera::LatencyGateResult last_latency_result_;
    MicroTradeCallback trade_cb_;
    FillNotifyCallback fill_notify_cb_;
    
    // Config
    bool enabled_ = true;
    double base_qty_ = 0.001;
    RoutingMode routing_mode_ = RoutingMode::MAKER_ONLY;
    
    // Stats
    std::atomic<uint64_t> trades_entered_{0};
    std::atomic<uint64_t> trades_exited_{0};
    std::atomic<uint64_t> orders_submitted_{0};  // v4.9.9: Order attempts (not fills)
    std::atomic<uint64_t> maker_timeouts_{0};    // v4.9.9: Timeout count
    std::atomic<uint64_t> probe_failures_{0};
    std::atomic<uint64_t> probe_confirms_{0};
    uint64_t winning_trades_ = 0;
    double total_pnl_bps_ = 0.0;
    uint64_t maker_fills_ = 0;
    uint64_t taker_fills_ = 0;
    
    // v4.9.8: Health tracking (for governor)
    uint64_t window_start_ns_ = 0;
    int window_trades_ = 0;
    int window_signals_ = 0;
    double window_gross_pnl_ = 0.0;
    double window_net_pnl_ = 0.0;
    double window_fees_ = 0.0;
    
    // Timing
    uint64_t last_trade_ts_ns_ = 0;
    uint64_t last_entry_attempt_ts_ = 0;  // v4.9.8: Prevent multiple entries per tick
    uint64_t startup_ts_ns_ = 0;          // v4.9.8: Engine startup time
    uint64_t order_submit_ts_ns_ = 0;     // v4.9.9: When order was submitted
    static constexpr uint64_t STARTUP_BLACKOUT_NS = 60'000'000'000ULL;  // 60 seconds
    static constexpr uint64_t COOLDOWN_NS = 500'000'000ULL;  // v4.9.8: 500ms (was 200ms)
    static constexpr uint64_t HEALTH_WINDOW_NS = 5ULL * 60 * 1'000'000'000ULL;  // 5 min
    
    // SOL state
    int sol_loss_streak_ = 0;
    double sol_daily_pnl_bps_ = 0.0;
    bool sol_auto_disabled_ = false;
    static constexpr int SOL_MAX_LOSS_STREAK = 3;
    static constexpr double SOL_MAX_DAILY_LOSS_BPS = -20.0;
    
    // Debug
    uint64_t tick_count_ = 0;
    MicroTick last_tick_{};
    double last_mid_ = 0.0;  // v4.9.10: Track last mid price for bootstrap probes
};

// =============================================================================
// IMPLEMENTATION
// =============================================================================

inline CryptoMicroScalpEngine::CryptoMicroScalpEngine(const std::string& symbol)
    : symbol_(symbol)
    , symbol_type_(parseSymbol(symbol.c_str()))
    , fee_config_(getFeeConfig(symbol.c_str()))
    , probe_params_(getProbeParams(symbol.c_str()))
    , entry_params_(getEntryParams(symbol.c_str()))
    , governor_(symbol_type_ == CryptoSymbol::BTCUSDT ? createBTCGovernor() :
                symbol_type_ == CryptoSymbol::ETHUSDT ? createETHGovernor() :
                symbol_type_ == CryptoSymbol::SOLUSDT ? createSOLGovernor() :
                createBTCGovernor())
    , size_config_(symbol_type_ == CryptoSymbol::BTCUSDT ? BTC_SIZE_CONFIG :
                   symbol_type_ == CryptoSymbol::ETHUSDT ? ETH_SIZE_CONFIG :
                   symbol_type_ == CryptoSymbol::SOLUSDT ? SOL_SIZE_CONFIG :
                   ETH_SIZE_CONFIG)
    , fee_loss_guard_(10, 3)  // 3+ fee-only losses in 10 trades → trigger
    , taker_guard_(symbol_type_ == CryptoSymbol::BTCUSDT ? 0.05 :
                   symbol_type_ == CryptoSymbol::ETHUSDT ? 0.18 :
                   symbol_type_ == CryptoSymbol::SOLUSDT ? 0.25 : 0.20)
    , maker_timeout_(symbol_type_ == CryptoSymbol::BTCUSDT ? BTC_TIMEOUT_CONFIG :
                     symbol_type_ == CryptoSymbol::ETHUSDT ? ETH_TIMEOUT_CONFIG :
                     symbol_type_ == CryptoSymbol::SOLUSDT ? SOL_TIMEOUT_CONFIG :
                     ETH_TIMEOUT_CONFIG)
{
    // v4.9.8: Record startup time for 60s blackout
    startup_ts_ns_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    // Initialize governor params from entry params
    gov_params_.entry_confidence_min = entry_params_.min_confidence;
    gov_params_.expectancy_min_bps = entry_params_.min_gross_edge_bps;
    gov_params_.confirmation_ticks = 1;
    gov_params_.maker_timeout_ms = fee_config_.maker_timeout_ms;
    gov_params_.gross_edge_min_bps = entry_params_.min_gross_edge_bps;
    gov_params_.net_exit_min_bps = 0.0;  // Must be net positive to exit winner
    gov_params_.max_hold_ms = probe_params_.confirm_max_hold_ms;
    
    // Set default routing per symbol
    if (symbol_type_ == CryptoSymbol::BTCUSDT) {
        routing_mode_ = RoutingMode::MAKER_ONLY;  // BTC: MAKER_ONLY (non-negotiable)
    } else {
        routing_mode_ = RoutingMode::HYBRID;      // ETH/SOL: HYBRID
    }
    
    printf("[MICROSCALP] Created %s v4.9.9 (TRADE_CB ON FILL + BTC TAKER-ONLY)\n", symbol_.c_str());
    if (symbol_type_ == CryptoSymbol::BTCUSDT) {
        printf("[MICROSCALP]   *** BTC RULE: TAKER-ONLY, MIN_EDGE=8.0bps ***\n");
    }
    printf("[MICROSCALP]   ROUTING: %s\n", routingModeStr(routing_mode_));
    printf("[MICROSCALP]   ENTRY: conf>=%.2f ofi>=%.2f gross_edge>=%.2f stable=%s\n",
           entry_params_.min_confidence, entry_params_.min_ofi, entry_params_.min_gross_edge_bps,
           entry_params_.allow_stable_regime ? "YES" : "NO");
    printf("[MICROSCALP]   TARGETS: TP=%.1fbps SL=%.1fbps\n",
           entry_params_.tp_bps, entry_params_.sl_bps);
    printf("[MICROSCALP]   SIZE_SCALE: min=%.2f max=%.2f\n",
           size_config_.min_size_mult, size_config_.max_size_mult);
    printf("[MICROSCALP]   TAKER_GUARD: thresh=%.0f%% FEE_LOSS_GUARD: 3/10\n",
           taker_guard_.threshold() * 100.0);
    printf("[MICROSCALP]   STARTUP_BLACKOUT: 60s (NO TRADES UNTIL SIGNALS STABLE)\n");
}

inline void CryptoMicroScalpEngine::onTick(const MicroTick& tick) {
    if (!enabled_) return;
    
    tick_count_++;
    last_tick_ = tick;
    last_mid_ = tick.mid;  // v4.9.10: Track for bootstrap probes
    
    // Update trackers
    double spread_bps = (tick.ask - tick.bid) / tick.mid * 10000.0;
    spread_tracker_.onTick(spread_bps);
    regime_detector_.onTick(tick.mid, tick.volatility, tick.ofi);
    
    // v4.9.8: GROSS edge (NO FEES)
    last_gross_edge_bps_ = calcGrossEdgeBps(tick);
    
    // Track signals for health metrics
    if (last_gross_edge_bps_ > entry_params_.min_gross_edge_bps) {
        window_signals_++;
    }
    
    // SOL regime
    if (symbol_type_ == CryptoSymbol::SOLUSDT) {
        sol_regime_detector_.onTick(tick.volatility, last_gross_edge_bps_, tick.ts_ns);
    }
    
    // Update health window
    if (tick.ts_ns - window_start_ns_ > HEALTH_WINDOW_NS) {
        // ═══════════════════════════════════════════════════════════════════
        // FIX #5: DON'T UPDATE GOVERNOR WHEN VENUE IS UNHEALTHY (v4.9.25)
        // When venue is HALTED or in RECOVERY_COOLDOWN:
        //   - Do NOT degrade alpha
        //   - Do NOT reduce confidence
        //   - Do NOT record fake losses
        // Only execution health changes, not alpha health.
        // ═══════════════════════════════════════════════════════════════════
        bool venue_healthy = Chimera::ExecutionGovernor::instance().isVenueHealthy();
        
        if (venue_healthy) {
            // Venue is healthy - update governor normally
            // Update health metrics
            health_.gross_edge_rate = (double)window_signals_ / 5.0;  // per minute
            health_.trade_rate = (double)window_trades_ / 5.0;
            health_.maker_fill_rate = (maker_fills_ > 0) 
                ? (double)maker_fills_ / (maker_fills_ + taker_fills_) : 1.0;
            health_.taker_ratio = taker_guard_.takerRatio();
            health_.fee_dominance = (window_gross_pnl_ > 0) 
                ? window_fees_ / window_gross_pnl_ : 0.0;
            health_.net_expectancy_bps = (window_trades_ > 0) 
                ? window_net_pnl_ / window_trades_ : 0.0;
            health_.trades = window_trades_;
            health_.fee_only_losses = fee_loss_guard_.feeOnlyLosses();
            
            // Run governor
            bool allow_relax = !fee_loss_guard_.shouldTrigger() && !taker_guard_.shouldForceMaker();
            governor_telem_ = governor_.step(gov_params_, health_, allow_relax);
            
            // COMPUTE GOVERNOR HEAT (one number for everything)
            governor_heat_ = computeGovernorHeat(gov_params_, governor_.bounds(),
                                                  governor_.relaxSteps(), 
                                                  symbol_type_ == CryptoSymbol::SOLUSDT ? 2 : 3);
            
            // Apply governor adjustments to entry params
            entry_params_.min_confidence = gov_params_.entry_confidence_min;
            fee_config_.maker_timeout_ms = gov_params_.maker_timeout_ms;
            
            // Force maker-only if guards trigger
            if (governor_telem_.forced_maker_only || taker_guard_.shouldForceMaker()) {
                routing_mode_ = RoutingMode::MAKER_ONLY;
            }
        } else {
            // Venue is unhealthy - FREEZE governor state (FIX #5)
            // Don't update health metrics, don't run governor step
            // This prevents alpha degradation during connectivity issues
        }
        
        // Reset window (always reset to track new data)
        window_start_ns_ = tick.ts_ns;
        window_trades_ = 0;
        window_signals_ = 0;
        window_gross_pnl_ = 0.0;
        window_net_pnl_ = 0.0;
        window_fees_ = 0.0;
    }
    
    // Log every 500 ticks (includes heat and size_mult for visibility)
    if (tick_count_ % 500 == 0) {
        double size_mult = sizeMultiplier();
        const char* venue_state = Chimera::venueStateToString(
            Chimera::ExecutionGovernor::instance().state());
        printf("[MICROSCALP][%s] tick=%llu state=%s gross_edge=%.2f spread=%.2f "
               "regime=%s conf=%.2f gov=%s heat=%.2f size_mult=%.2f routing=%s venue=%s\n",
               symbol_.c_str(), (unsigned long long)tick_count_,
               microStateStr(state_), last_gross_edge_bps_, spread_bps,
               regimeStr(regime_detector_.regime()), regime_detector_.confidence(),
               governorStateStr(governor_telem_.state), governor_heat_, size_mult,
               routingModeStr(routing_mode_), venue_state);
    }
    
    // v4.9.25: Tick the ExecutionGovernor to process recovery timers
    if (tick_count_ % 100 == 0) {
        Chimera::ExecutionGovernor::instance().tick();
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
inline void CryptoMicroScalpEngine::handleFlat(const MicroTick& tick) {
    // v4.9.25 FIX #1: FAIL-SAFE venue state check (belt and suspenders)
    // checkEntryFilters will also catch this, but this makes intent explicit
    if (!Chimera::ExecutionGovernor::instance().canSubmit(symbol_.c_str())) {
        const char* reason = Chimera::ExecutionGovernor::instance().getBlockReason(symbol_.c_str());
        snprintf(last_block_reason_, sizeof(last_block_reason_), "%s", 
                 reason ? reason : "VENUE_BLOCKED");
        return;  // CRITICAL: Do not attempt entry during venue issues
    }
    
    // v4.9.8: HARD 60-SECOND STARTUP BLACKOUT - NO TRADES EVER
    uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    uint64_t uptime_ns = now_ns - startup_ts_ns_;
    if (uptime_ns < STARTUP_BLACKOUT_NS) {
        int remain_sec = (STARTUP_BLACKOUT_NS - uptime_ns) / 1'000'000'000ULL;
        snprintf(last_block_reason_, sizeof(last_block_reason_), "STARTUP_BLACKOUT %ds", remain_sec);
        return;
    }
    
    // v4.9.8: CRITICAL - Only 1 entry attempt per tick (prevents spam)
    if (tick.ts_ns == last_entry_attempt_ts_) {
        snprintf(last_block_reason_, sizeof(last_block_reason_), "SAME_TICK_BLOCK");
        return;
    }
    
    // v4.9.8: Check for stale pending fill (maker order timeout)
    if (pending_fill_) {
        uint64_t pending_age_ms = (tick.ts_ns - last_trade_ts_ns_) / 1'000'000;
        if (pending_age_ms > static_cast<uint64_t>(maker_timeout_.currentTimeout())) {
            // Maker order timed out - cancel and reset
            pending_fill_ = false;
            last_trade_ts_ns_ = tick.ts_ns;  // v4.9.8: Reset cooldown timer on timeout
            snprintf(last_block_reason_, sizeof(last_block_reason_), "MAKER_TIMEOUT %llums", 
                     (unsigned long long)pending_age_ms);
            printf("[MICROSCALP][%s] MAKER_TIMEOUT: age=%llums, resetting pending_fill + cooldown\n",
                   symbol_.c_str(), (unsigned long long)pending_age_ms);
            // Note: Order cancellation should be handled by BinanceEngine
            return;  // v4.9.8: Don't immediately re-enter, wait for cooldown
        } else {
            snprintf(last_block_reason_, sizeof(last_block_reason_), "PENDING_FILL %llums", 
                     (unsigned long long)pending_age_ms);
            return;
        }
    }
    
    double gross_edge = last_gross_edge_bps_;
    
    // v4.9.8: Entry based on GROSS edge only (no fee subtraction)
    if (checkEntryFilters(tick, gross_edge)) {
        enter(tick, gross_edge);
    }
}

// =============================================================================
// STATE: PROBE - Evaluating post-entry structure
// =============================================================================
inline void CryptoMicroScalpEngine::handleProbe(const MicroTick& tick) {
    double spread_bps = (tick.ask - tick.bid) / tick.mid * 10000.0;
    int age_ms = (tick.ts_ns - snapshot_.ts_ns) / 1'000'000;
    double gross_pnl = (tick.bid - snapshot_.price) / snapshot_.price * 10000.0;
    
    // ═══════════════════════════════════════════════════════════════════════
    // FAIL CONDITIONS
    // ═══════════════════════════════════════════════════════════════════════
    
    // Spread expansion
    if (spread_bps > snapshot_.spread_bps * probe_params_.spread_expand) {
        probe_failures_++;
        exit(tick, ExitReason::SPREAD_EXPAND);
        return;
    }
    
    // Edge decay
    if (last_gross_edge_bps_ < snapshot_.gross_edge_bps * probe_params_.edge_drop_ratio) {
        probe_failures_++;
        exit(tick, ExitReason::EDGE_DECAY);
        return;
    }
    
    // Hard stop loss (on gross, not net)
    double sl = stopLossBps();
    if (gross_pnl <= -sl) {
        probe_failures_++;
        exit(tick, ExitReason::STOP_LOSS);
        return;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // CONFIRM CONDITIONS
    // ═══════════════════════════════════════════════════════════════════════
    
    // v4.9.8: For CONFIRM, need net positive (must clear fees)
    double net_pnl = calcNetPnlBps(tick);
    
    if (age_ms >= probe_params_.probe_min_ms &&
        tick.volatility > snapshot_.vol * probe_params_.vol_confirm &&
        net_pnl > 0.0) {  // v4.9.8: Must be net positive
        
        state_ = MicroState::CONFIRM;
        confirm_ts_ns_ = tick.ts_ns;
        probe_confirms_++;
        
        printf("[MICROSCALP][%s] PROBE_CONFIRM: age=%dms gross=%.2f net=%.2f\n",
               symbol_.c_str(), age_ms, gross_pnl, net_pnl);
        return;
    }
    
    // Timeout
    if (age_ms > probe_params_.probe_max_ms) {
        // ═══════════════════════════════════════════════════════════════════
        // FIX #2: PROBE_TIMEOUT ≠ LOSS when venue is unhealthy (v4.9.25)
        // A timeout due to venue halt is:
        //   - NOT a trade
        //   - NOT a loss
        //   - NOT an expectancy datapoint
        // This prevents poisoning statistics during connectivity issues.
        // ═══════════════════════════════════════════════════════════════════
        bool venue_healthy = Chimera::ExecutionGovernor::instance().isVenueHealthy();
        
        if (venue_healthy) {
            // Venue is healthy - this is a real probe timeout
            probe_failures_++;
            
            // FIX #4: Notify governor for potential symbol freeze
            Chimera::ExecutionGovernor::instance().on_probe_timeout(symbol_.c_str());
            
            // Record as normal exit (with PnL attribution)
            exit(tick, ExitReason::PROBE_TIMEOUT);
        } else {
            // Venue is unhealthy - do NOT record as failure, just reset
            printf("[MICROSCALP][%s] PROBE_TIMEOUT during VENUE_HALT - NO PnL recorded (not a trade)\n",
                   symbol_.c_str());
            
            // Reset state without recording PnL (FIX #2)
            state_ = MicroState::FLAT;
            pending_fill_ = false;
            last_trade_ts_ns_ = tick.ts_ns;
            
            // Record as VETO, not loss
            snprintf(last_block_reason_, sizeof(last_block_reason_), "VENUE_HALT_VETO");
        }
        return;
    }
}

// =============================================================================
// STATE: CONFIRM - Winner window
// =============================================================================
inline void CryptoMicroScalpEngine::handleConfirm(const MicroTick& tick) {
    double spread_bps = (tick.ask - tick.bid) / tick.mid * 10000.0;
    int total_age_ms = (tick.ts_ns - snapshot_.ts_ns) / 1'000'000;
    double gross_pnl = (tick.bid - snapshot_.price) / snapshot_.price * 10000.0;
    double net_pnl = calcNetPnlBps(tick);
    
    // Spread break
    if (spread_bps > snapshot_.spread_bps * probe_params_.spread_expand) {
        exit(tick, ExitReason::SPREAD_BREAK);
        return;
    }
    
    // v4.9.8: Take profit based on NET profit (must clear fees)
    double tp = effectiveTP(tick);
    if (net_pnl >= tp) {
        exit(tick, ExitReason::TAKE_PROFIT);
        return;
    }
    
    // Stop loss (still on gross to prevent over-paying fees)
    double sl = stopLossBps();
    if (gross_pnl <= -sl) {
        exit(tick, ExitReason::STOP_LOSS);
        return;
    }
    
    // Max hold
    if (total_age_ms >= probe_params_.confirm_max_hold_ms) {
        exit(tick, ExitReason::TIME_STOP);
        return;
    }
}

// =============================================================================
// ENTRY FILTERS (v4.9.8: FEE-AGNOSTIC, v4.9.25: VENUE STATE GATING)
// =============================================================================
inline bool CryptoMicroScalpEngine::checkEntryFilters(const MicroTick& tick, double gross_edge_bps) {
    uint64_t now = tick.ts_ns;
    double spread_bps = (tick.ask - tick.bid) / tick.mid * 10000.0;
    
    // ═══════════════════════════════════════════════════════════════════════
    // FIX #1: VENUE STATE HARD GATE (v4.9.25)
    // This MUST be the FIRST check - prevents all order attempts when venue
    // is unhealthy. This eliminates the feedback loop where:
    //   WS hiccup → Venue HALTED → Strategy still fires → Orders blocked →
    //   Fake losses → Alarms → Repeat
    // ═══════════════════════════════════════════════════════════════════════
    if (!Chimera::ExecutionGovernor::instance().canSubmit(symbol_.c_str())) {
        const char* reason = Chimera::ExecutionGovernor::instance().getBlockReason(symbol_.c_str());
        snprintf(last_block_reason_, sizeof(last_block_reason_), "%s", 
                 reason ? reason : "VENUE_BLOCKED");
        return false;  // DO NOT ENTER, DO NOT PROBE, DO NOT LOG PnL
    }
    
    // Warmup
    if (tick_count_ < entry_params_.warmup_ticks) {
        snprintf(last_block_reason_, sizeof(last_block_reason_), "WARMUP %llu/%d", 
                 (unsigned long long)tick_count_, entry_params_.warmup_ticks);
        return false;
    }
    
    // Cooldown
    if (last_trade_ts_ns_ > 0 && now - last_trade_ts_ns_ < COOLDOWN_NS) {
        int cd_remain_ms = (COOLDOWN_NS - (now - last_trade_ts_ns_)) / 1'000'000;
        snprintf(last_block_reason_, sizeof(last_block_reason_), "COOLDOWN %dms", cd_remain_ms);
        return false;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.9.10: LATENCY GATE - Block trades during degraded latency
    // This kills trades during: CPU contention, GC, VPS noise, network issues
    // ═══════════════════════════════════════════════════════════════════════
    if (latency_snapshot_cb_) {
        auto lat_snap = latency_snapshot_cb_();
        last_latency_result_ = latency_gate_.checkWithEdge(lat_snap, gross_edge_bps);
        
        if (!last_latency_result_.allowed) {
            snprintf(last_block_reason_, sizeof(last_block_reason_), "LAT_%s", 
                     last_latency_result_.block_reason);
            return false;
        }
        
        // v4.9.10: Override routing if latency dictates TAKER_ONLY
        // (exec_mode will be checked in effectiveRouting())
    }
    
    // Governor clamp check
    if (governor_telem_.state != GovernorState::NORMAL && 
        governor_telem_.state != GovernorState::RELAX_CAP_REACHED) {
        snprintf(last_block_reason_, sizeof(last_block_reason_), "GOV_CLAMP");
        return false;
    }
    
    // Confidence (governor-adjusted)
    if (regime_detector_.confidence() < entry_params_.min_confidence) {
        snprintf(last_block_reason_, sizeof(last_block_reason_), "LOW_CONF %.2f", regime_detector_.confidence());
        return false;
    }
    
    // Regime permission
    MicroRegime regime = regime_detector_.regime();
    if (regime == MicroRegime::STABLE && !entry_params_.allow_stable_regime) {
        snprintf(last_block_reason_, sizeof(last_block_reason_), "STABLE_REGIME");
        return false;
    }
    
    // v4.9.8: GROSS edge gate (NO FEES SUBTRACTED)
    if (gross_edge_bps < entry_params_.min_gross_edge_bps) {
        snprintf(last_block_reason_, sizeof(last_block_reason_), "LOW_EDGE %.1fbps", gross_edge_bps);
        return false;
    }
    
    // OFI threshold
    if (std::fabs(tick.ofi) < entry_params_.min_ofi) {
        snprintf(last_block_reason_, sizeof(last_block_reason_), "LOW_OFI %.3f", tick.ofi);
        return false;
    }
    
    // Latency
    if (tick.latency_ms > latencyThresholdMs()) {
        snprintf(last_block_reason_, sizeof(last_block_reason_), "HIGH_LAT %.1fms", tick.latency_ms);
        return false;
    }
    
    // Spread anomaly
    if (spread_tracker_.isAnomaly(spread_bps)) {
        snprintf(last_block_reason_, sizeof(last_block_reason_), "SPREAD_ANOMALY %.1f", spread_bps);
        return false;
    }
    
    // Loss clustering
    if (loss_cluster_.shouldPause()) {
        snprintf(last_block_reason_, sizeof(last_block_reason_), "LOSS_CLUSTER");
        return false;
    }
    
    // Fee loss guard (if too many fee-only losses, pause)
    if (fee_loss_guard_.shouldTrigger()) {
        snprintf(last_block_reason_, sizeof(last_block_reason_), "FEE_GUARD");
        return false;
    }
    
    // Symbol-specific
    switch (symbol_type_) {
        case CryptoSymbol::BTCUSDT:
            // v4.9.9: BTC TAKER-ONLY HIGH-EDGE RULE
            // BTC maker orders don't fill (queue too deep), so we only use taker
            // Require 8 bps to cover round-trip fees (~4 bps each way)
            {
                constexpr double BTC_MIN_TAKER_EDGE_BPS = 8.0;
                if (gross_edge_bps < BTC_MIN_TAKER_EDGE_BPS) {
                    snprintf(last_block_reason_, sizeof(last_block_reason_), 
                             "BTC_LOW_EDGE %.1f<%.1f", gross_edge_bps, BTC_MIN_TAKER_EDGE_BPS);
                    return false;
                }
            }
            break;
            
        case CryptoSymbol::ETHUSDT:
            if (tick.ofi > 0 && tick.pressure < 0.5) {
                snprintf(last_block_reason_, sizeof(last_block_reason_), "ETH_PRESS %.2f", tick.pressure);
                return false;
            }
            if (tick.ofi < 0 && tick.pressure > -0.5) {
                snprintf(last_block_reason_, sizeof(last_block_reason_), "ETH_PRESS %.2f", tick.pressure);
                return false;
            }
            break;
            
        case CryptoSymbol::SOLUSDT:
            if (sol_auto_disabled_) {
                snprintf(last_block_reason_, sizeof(last_block_reason_), "SOL_DISABLED");
                return false;
            }
            if (!sol_regime_detector_.canTrade()) {
                snprintf(last_block_reason_, sizeof(last_block_reason_), "SOL_REGIME");
                return false;
            }
            if (tick.volatility < 0.5) {
                snprintf(last_block_reason_, sizeof(last_block_reason_), "SOL_LOW_VOL %.2f", tick.volatility);
                return false;
            }
            break;
            
        default:
            break;
    }
    
    // Ready to trade!
    snprintf(last_block_reason_, sizeof(last_block_reason_), "READY");
    return true;
}

// =============================================================================
// ENTRY
// =============================================================================
inline void CryptoMicroScalpEngine::enter(const MicroTick& tick, double gross_edge_bps) {
    // v4.9.8: Mark this tick as attempted (prevents duplicate entries)
    last_entry_attempt_ts_ = tick.ts_ns;
    
    double qty = base_qty_ * sizeMultiplier();
    double spread_bps = (tick.ask - tick.bid) / tick.mid * 10000.0;
    
    if (qty <= 0.0) {
        snprintf(last_block_reason_, sizeof(last_block_reason_), "ZERO_QTY %.4f", 
                 base_qty_ * sizeMultiplier());
        return;
    }
    
    // Create entry snapshot with GROSS edge
    snapshot_.price = tick.mid;
    snapshot_.gross_edge_bps = gross_edge_bps;
    snapshot_.spread_bps = spread_bps;
    snapshot_.vol = tick.volatility;
    snapshot_.ofi = tick.ofi;
    snapshot_.ts_ns = tick.ts_ns;
    snapshot_.fill_type = FillType::UNKNOWN;
    snapshot_.qty = qty;  // v4.9.9: Store qty for fill callback
    
    // v4.9.8: Determine effective routing
    RoutingMode effective_routing = effectiveRouting(gross_edge_bps);
    
    // Send order
    double limit_price = (effective_routing != RoutingMode::TAKER_ONLY) ? tick.bid : 0.0;
    if (order_cb_) {
        order_cb_(symbol_.c_str(), true, qty, limit_price, effective_routing);
    }
    
    // v4.9.9: Track order submission time
    order_submit_ts_ns_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    orders_submitted_++;
    
    if (effective_routing == RoutingMode::TAKER_ONLY) {
        // TAKER: Immediate fill assumed
        entry_fill_type_ = FillType::TAKER;
        snapshot_.fill_type = FillType::TAKER;
        state_ = MicroState::PROBE;
        taker_fills_++;
        taker_guard_.recordFill(true);
        trades_entered_++;
        window_trades_++;
        
        // v4.9.9: trade_cb_ fires HERE for taker (immediate fill)
        if (trade_cb_) {
            trade_cb_(symbol_.c_str(), +1, qty, tick.ask, 0.0);
        }
        
        printf("[MICROSCALP][%s] ENTER (TAKER) gross_edge=%.2f qty=%.6f → PROBE\n",
               symbol_.c_str(), gross_edge_bps, qty);
    } else {
        // MAKER: Wait for fill callback
        pending_fill_ = true;
        // v4.9.9: DO NOT call trade_cb_ here - wait for onFill()
        // v4.9.9: DO NOT increment trades_entered_ here - wait for onFill()
        
        printf("[MICROSCALP][%s] ORDER_SUBMITTED (MAKER) gross_edge=%.2f qty=%.6f → WAITING_FOR_FILL\n",
               symbol_.c_str(), gross_edge_bps, qty);
    }
    
    // Update cooldown timestamp
    last_trade_ts_ns_ = order_submit_ts_ns_;
}

// =============================================================================
// EXIT
// =============================================================================
inline void CryptoMicroScalpEngine::exit(const MicroTick& tick, ExitReason reason) {
    double qty = snapshot_.qty;  // v4.9.9: Use stored qty
    double exit_price = tick.bid;
    
    if (qty <= 0.0) {
        state_ = MicroState::FLAT;
        return;
    }
    
    // Exit always uses taker for speed
    if (order_cb_) {
        order_cb_(symbol_.c_str(), false, qty, 0.0, RoutingMode::TAKER_ONLY);
    }
    exit_fill_type_ = FillType::TAKER;
    
    // Compute PnL with attribution
    PnLAttribution attr;
    attr.compute(snapshot_.price, exit_price, snapshot_.spread_bps,
                 snapshot_.fill_type, exit_fill_type_, fee_config_);
    attr.log(symbol_.c_str(), reason);
    
    total_pnl_bps_ += attr.net_pnl_bps;
    trades_exited_++;
    
    if (attr.net_pnl_bps > 0) winning_trades_++;
    
    // Update health window
    window_gross_pnl_ += attr.gross_pnl_bps;
    window_net_pnl_ += attr.net_pnl_bps;
    window_fees_ += attr.fee_cost;
    
    // Record for fee loss guard
    fee_loss_guard_.recordTrade(attr.gross_pnl_bps, attr.net_pnl_bps, 
                                 tick.ts_ns, snapshot_.fill_type == FillType::TAKER);
    
    // Loss clustering
    loss_cluster_.recordTrade(attr.net_pnl_bps < 0, tick.ts_ns);
    
    // SOL tracking
    if (symbol_type_ == CryptoSymbol::SOLUSDT) {
        sol_daily_pnl_bps_ += attr.net_pnl_bps;
        if (attr.net_pnl_bps < 0) {
            sol_loss_streak_++;
            if (sol_loss_streak_ >= SOL_MAX_LOSS_STREAK) {
                sol_auto_disabled_ = true;
            }
        } else {
            sol_loss_streak_ = 0;
        }
        if (sol_daily_pnl_bps_ <= SOL_MAX_DAILY_LOSS_BPS) {
            sol_auto_disabled_ = true;
        }
    }
    
    if (trade_cb_) {
        trade_cb_(symbol_.c_str(), -1, qty, exit_price, attr.net_pnl_bps);
    }
    
    int total_age_ms = (tick.ts_ns - snapshot_.ts_ns) / 1'000'000;
    printf("[MICROSCALP][%s] EXIT @ %.2f net=%.2fbps age=%dms state=%s winrate=%.1f%%\n",
           symbol_.c_str(), exit_price, attr.net_pnl_bps, total_age_ms,
           microStateStr(state_), winRate() * 100.0);
    
    state_ = MicroState::FLAT;
    last_trade_ts_ns_ = tick.ts_ns;
}

// =============================================================================
// FILL HANDLING
// =============================================================================
inline void CryptoMicroScalpEngine::onFill(FillType fill_type, double filled_price) {
    if (pending_fill_) {
        entry_fill_type_ = fill_type;
        snapshot_.fill_type = fill_type;
        snapshot_.price = filled_price;
        pending_fill_ = false;
        state_ = MicroState::PROBE;
        
        // v4.9.9: NOW we count this as a real trade
        trades_entered_++;
        window_trades_++;
        
        if (fill_type == FillType::MAKER) {
            maker_fills_++;
            taker_guard_.recordFill(false);
        } else {
            taker_fills_++;
            taker_guard_.recordFill(true);
        }
        
        // Update dynamic timeout based on fill rate
        maker_timeout_.adapt(taker_guard_.takerRatio() < 1.0 
            ? 1.0 - taker_guard_.takerRatio() : 0.0);
        
        // v4.9.9: CRITICAL - trade_cb_ fires HERE for maker orders (on actual fill)
        if (trade_cb_) {
            trade_cb_(symbol_.c_str(), +1, snapshot_.qty, filled_price, 0.0);
        }
        
        printf("[MICROSCALP][%s] FILL: %s @ %.2f → PROBE (timeout=%dms) trades=%llu\n",
               symbol_.c_str(), fillTypeStr(fill_type), filled_price,
               maker_timeout_.currentTimeout(),
               (unsigned long long)trades_entered_.load());
    }
}

inline void CryptoMicroScalpEngine::onMakerTimeout() {
    if (pending_fill_) {
        double gross_edge = calcGrossEdgeBps(last_tick_);
        
        // v4.9.8: For HYBRID, check if edge is high enough for taker
        if (routing_mode_ == RoutingMode::HYBRID && 
            gross_edge >= fee_config_.hybrid_taker_edge_bps) {
            // Fall back to taker
            printf("[MICROSCALP][%s] MAKER_TIMEOUT: taker fallback (edge=%.2f >= %.2f)\n",
                   symbol_.c_str(), gross_edge, fee_config_.hybrid_taker_edge_bps);
            // Order sender should handle taker conversion
        } else {
            // ABORT - not enough edge for taker
            pending_fill_ = false;
            state_ = MicroState::FLAT;
            printf("[MICROSCALP][%s] MAKER_ABORT: edge=%.2f < %.2f threshold\n",
                   symbol_.c_str(), gross_edge, fee_config_.hybrid_taker_edge_bps);
        }
    }
}

// =============================================================================
// CALCULATIONS
// =============================================================================

// v4.9.8: GROSS edge - NO FEES (this is the key fix)
inline double CryptoMicroScalpEngine::calcGrossEdgeBps(const MicroTick& tick) const {
    double ofi_floor = entry_params_.min_ofi;
    double abs_ofi = std::fabs(tick.ofi);
    
    if (abs_ofi < ofi_floor * 0.5) return 0.0;  // Minimum activity
    
    // Edge multiplier per symbol
    double edge_mult = 0.0;
    double pressure_bonus = 0.0;
    
    switch (symbol_type_) {
        case CryptoSymbol::BTCUSDT:
            // BTC: Speed edge - OFI 0.8+ yields ~6-10 bps
            edge_mult = 10.0;
            if (abs_ofi > 0.6 && std::fabs(tick.pressure) > 0.5) {
                pressure_bonus = 1.5;
            }
            break;
            
        case CryptoSymbol::ETHUSDT:
            // ETH: More volatile, larger moves
            edge_mult = 12.0;
            if (abs_ofi > 0.5 && std::fabs(tick.pressure) > 0.4) {
                pressure_bonus = 2.0;
            }
            break;
            
        case CryptoSymbol::SOLUSDT:
            // SOL: Most volatile
            edge_mult = 15.0;
            if (abs_ofi > 0.6 && std::fabs(tick.pressure) > 0.5) {
                pressure_bonus = 2.5;
            }
            break;
            
        default:
            edge_mult = 10.0;
            break;
    }
    
    // Base edge from OFI
    double ofi_edge = abs_ofi * edge_mult;
    
    // Volatility bonus
    double vol_mult = 1.0;
    if (tick.volatility > 0.8) {
        vol_mult = 1.0 + (tick.volatility - 0.8) * 0.3;
    }
    
    // Alignment bonus
    bool aligned = (tick.ofi > 0 && tick.pressure > 0) || 
                   (tick.ofi < 0 && tick.pressure < 0);
    if (aligned) pressure_bonus *= 1.2;
    
    double gross_edge = (ofi_edge + pressure_bonus) * vol_mult;
    
    // Cap at reasonable max
    double max_edge = (symbol_type_ == CryptoSymbol::SOLUSDT) ? 20.0 : 
                      (symbol_type_ == CryptoSymbol::ETHUSDT) ? 15.0 : 12.0;
    
    return std::min(gross_edge, max_edge);
}

inline double CryptoMicroScalpEngine::calcNetPnlBps(const MicroTick& tick) const {
    if (snapshot_.price <= 0.0) return 0.0;
    
    double gross_pnl = (tick.bid - snapshot_.price) / snapshot_.price * 10000.0;
    double fees = totalFeeCostBps();
    double spread = spreadCostBps(tick);
    
    return gross_pnl - fees - spread;
}

inline double CryptoMicroScalpEngine::totalFeeCostBps() const {
    double entry_fee = (snapshot_.fill_type == FillType::MAKER) 
        ? fee_config_.maker_fee_bps : fee_config_.taker_fee_bps;
    double exit_fee = fee_config_.taker_fee_bps;  // Always taker exit
    return entry_fee + exit_fee;
}

inline double CryptoMicroScalpEngine::spreadCostBps(const MicroTick& tick) const {
    return (tick.ask - tick.bid) / tick.mid * 10000.0 * 0.5;
}

inline double CryptoMicroScalpEngine::effectiveTP(const MicroTick& tick) const {
    // TP is already net-based from entry_params
    double base_tp = entry_params_.tp_bps;
    
    // Add expansion in CONFIRM based on volatility
    if (state_ == MicroState::CONFIRM && tick.volatility > snapshot_.vol) {
        base_tp += base_tp * probe_params_.tp_expansion;
    }
    
    return base_tp;
}

inline double CryptoMicroScalpEngine::stopLossBps() const {
    return entry_params_.sl_bps;
}

inline double CryptoMicroScalpEngine::sizeMultiplier() const {
    // ═══════════════════════════════════════════════════════════════════════
    // GOVERNOR HEAT → SIZE SCALER (FINAL LAYER)
    // ═══════════════════════════════════════════════════════════════════════
    // Core principle: Heat controls aggressiveness, not permissions.
    //   Heat ↑ → size ↓
    //   Heat ↓ → size ↑
    //   Trades may still occur when hot, but small and survivable
    // ═══════════════════════════════════════════════════════════════════════
    
    // Base symbol multiplier (market-specific risk)
    double symbol_mult = (symbol_type_ == CryptoSymbol::BTCUSDT) ? 0.5 :
                        (symbol_type_ == CryptoSymbol::ETHUSDT) ? 0.7 : 0.4;
    
    // Regime multiplier (volatility adjustment)
    MicroRegime r = regime_detector_.regime();
    double regime_mult = (r == MicroRegime::STABLE) ? 0.8 :
                        (r == MicroRegime::BURST) ? 1.0 : 0.5;
    
    // GOVERNOR HEAT SCALING (THE KEY)
    // Uses piecewise function for stable regimes (no jitter)
    double heat_mult = computeFinalSizeMultiplier(governor_heat_, size_config_);
    
    // Fee loss guard (additional reduction if fee-dominated losses detected)
    double guard_mult = fee_loss_guard_.shouldTrigger() ? 0.5 : 1.0;
    
    // Combine all multipliers
    double final_mult = symbol_mult * regime_mult * heat_mult * guard_mult;
    
    // SAFETY CHECK (log warning instead of assert to avoid crashes)
    if (final_mult > 1.0) {
        printf("[WARN] sizeMultiplier exceeded 1.0: %.4f, clamping\n", final_mult);
        final_mult = 1.0;
    }
    if (final_mult < size_config_.min_size_mult * 0.5 && final_mult > 0.0) {
        printf("[WARN] sizeMultiplier very low: %.4f (min=%.2f)\n", 
               final_mult, size_config_.min_size_mult * 0.5);
    }
    
    return final_mult;
}

inline double CryptoMicroScalpEngine::latencyThresholdMs() const {
    switch (symbol_type_) {
        case CryptoSymbol::BTCUSDT: return 0.8;
        case CryptoSymbol::ETHUSDT: return 1.2;
        case CryptoSymbol::SOLUSDT: return 1.0;
        default: return 1.0;
    }
}

// v4.9.8: Determine effective routing based on edge
inline RoutingMode CryptoMicroScalpEngine::effectiveRouting(double gross_edge_bps) const {
    // v4.9.10: LATENCY-FORCED TAKER_ONLY
    // If latency is too high for maker orders (queue position lost before arrival),
    // force taker-only regardless of other settings
    if (last_latency_result_.exec_mode == Chimera::ExecMode::TAKER_ONLY) {
        // Still check if we have enough edge for taker
        if (gross_edge_bps >= fee_config_.hybrid_taker_edge_bps) {
            return RoutingMode::TAKER_ONLY;
        }
        // Edge too low for taker, but latency too high for maker = SKIP
        // This will be caught by the edge check in checkEntryFilters
    }
    
    // v4.9.9: BTC ALWAYS uses TAKER - maker orders never fill
    if (symbol_type_ == CryptoSymbol::BTCUSDT) {
        return RoutingMode::TAKER_ONLY;
    }
    
    // If forced maker-only by guards, use it
    if (routing_mode_ == RoutingMode::MAKER_ONLY) {
        return RoutingMode::MAKER_ONLY;
    }
    
    // HYBRID mode: maker-first, but use taker if edge is high enough
    if (routing_mode_ == RoutingMode::HYBRID) {
        if (gross_edge_bps >= fee_config_.hybrid_taker_edge_bps * 1.5) {
            // Edge is very high - can afford taker
            return RoutingMode::TAKER_ONLY;
        }
        // Default to maker-first for HYBRID
        return RoutingMode::MAKER_FIRST;
    }
    
    return routing_mode_;
}

} // namespace Crypto
} // namespace Chimera
