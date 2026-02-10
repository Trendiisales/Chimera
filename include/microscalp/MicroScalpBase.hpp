#pragma once
// =============================================================================
// MicroScalpBase.hpp - Shared Base for All MicroScalp Engines
// =============================================================================
// DESIGN PRINCIPLES (ALL VARIANTS):
//   1. Tick-driven only (no candles)
//   2. Edge ≪ 2 bps
//   3. Tight TP, tighter SL
//   4. Fast exits dominate
//   5. Kill-switch first, optimisation second
//   6. Instrument allow-list only
//   7. Logs every decision, not just trades
//
// ARCHITECTURE:
//   MicroScalpBase (abstract)
//    ├── [REMOVED] - CFD only
//    ├── IndexMicroScalp   (NAS100/US30)
//    └── FXMicroScalp      (EURUSD/GBPUSD)
//
// v4.9.3: Initial base class
// =============================================================================

#include <atomic>
#include <string>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <functional>

namespace Chimera {
namespace MicroScalp {

// =============================================================================
// Common Tick Structure - shared across all engines
// =============================================================================
struct BaseTick {
    double bid;
    double ask;
    double mid;
    double ofi;              // Order flow imbalance [-1, +1]
    double pressure;         // Buy/sell pressure [-1, +1]
    double volatility;       // Normalized volatility
    double spread_bps;       // Spread in basis points
    double latency_ms;       // Network latency
    uint64_t ts_ns;          // Timestamp nanoseconds
    
    // Session flags (for Index/FX)
    bool session_ny;         // NY session active
    bool session_london;     // London session active
    bool session_asia;       // Asia session active
    
    // Derived signals
    double momentum;         // Price momentum
    double spread_compression; // Spread tightening signal
    bool pressure_aligned;   // OFI and pressure agree
    bool pressure_persistent; // Pressure held for X ms
    bool regime_impulse;     // In impulse regime
};

// =============================================================================
// Order Callback - wired to execution layer
// =============================================================================
using OrderCallback = std::function<void(
    const char* symbol,
    bool is_buy,
    double qty,
    bool is_maker  // true = post-only, false = market
)>;

// =============================================================================
// Trade Callback - for GUI/ML logging
// =============================================================================
using TradeCallback = std::function<void(
    const char* symbol,
    const char* engine,
    int8_t side,
    double qty,
    double price,
    double pnl_bps
)>;

// =============================================================================
// Kill Reasons
// =============================================================================
enum class KillReason : uint8_t {
    LOSS_STREAK     = 0,
    DAILY_CAP       = 1,
    LATENCY_BREACH  = 2,
    SPREAD_ANOMALY  = 3,
    SESSION_END     = 4,
    IDLE_TIMEOUT    = 5,
    MANUAL          = 6
};

inline const char* killReasonStr(KillReason r) {
    switch (r) {
        case KillReason::LOSS_STREAK:    return "LOSS_STREAK";
        case KillReason::DAILY_CAP:      return "DAILY_CAP";
        case KillReason::LATENCY_BREACH: return "LATENCY_BREACH";
        case KillReason::SPREAD_ANOMALY: return "SPREAD_ANOMALY";
        case KillReason::SESSION_END:    return "SESSION_END";
        case KillReason::IDLE_TIMEOUT:   return "IDLE_TIMEOUT";
        case KillReason::MANUAL:         return "MANUAL";
    }
    return "UNKNOWN";
}

// =============================================================================
// MicroScalpBase - Abstract Base Class
// =============================================================================
class MicroScalpBase {
public:
    virtual ~MicroScalpBase() = default;
    
    // Pure virtual - each engine implements
    virtual void onTick(const BaseTick& tick) = 0;
    virtual const char* engineName() const = 0;
    virtual const char* symbol() const = 0;
    
    // Common interface
    bool isEnabled() const { return enabled_; }
    void setEnabled(bool e) { enabled_ = e; }
    bool hasPosition() const { return in_position_; }
    
    // Stats
    int tradesToday() const { return trades_today_; }
    int lossStreak() const { return loss_streak_; }
    double pnlTodayBps() const { return pnl_today_bps_; }
    uint64_t ticksProcessed() const { return ticks_processed_; }
    
    // Callbacks
    void setOrderCallback(OrderCallback cb) { order_cb_ = std::move(cb); }
    void setTradeCallback(TradeCallback cb) { trade_cb_ = std::move(cb); }
    
    // Daily reset
    virtual void resetDaily() {
        trades_today_ = 0;
        loss_streak_ = 0;
        pnl_today_bps_ = 0.0;
        enabled_ = true;
        last_trade_ts_ = 0;
        printf("[%s][%s] Daily reset - re-enabled\n", engineName(), symbol());
    }

protected:
    // ==========================================================================
    // KILL SWITCH - emit telemetry and disable
    // ==========================================================================
    void disable(KillReason reason) {
        enabled_ = false;
        printf("\n[MICROSCALP-KILL]\n"
               "  engine=%s\n"
               "  symbol=%s\n"
               "  reason=%s\n"
               "  trades=%d\n"
               "  pnl=%.2fbps\n"
               "  loss_streak=%d\n"
               "  latency=%.2fms\n\n",
               engineName(),
               symbol(),
               killReasonStr(reason),
               trades_today_,
               pnl_today_bps_,
               loss_streak_,
               last_latency_ms_);
    }
    
    // ==========================================================================
    // RISK CHECKS - shared across all engines
    // ==========================================================================
    bool riskOK(int max_loss_streak, double daily_cap_bps) const {
        if (!enabled_) return false;
        if (loss_streak_ >= max_loss_streak) return false;
        if (pnl_today_bps_ <= daily_cap_bps) return false;
        return true;
    }
    
    bool latencyOK(double max_ms) const {
        return last_latency_ms_ <= max_ms;
    }
    
    bool spreadOK(double spread_bps, double max_spread_bps) const {
        return spread_bps <= max_spread_bps;
    }
    
    // ==========================================================================
    // TRADE TRACKING
    // ==========================================================================
    void recordTrade(double pnl_bps) {
        trades_today_++;
        pnl_today_bps_ += pnl_bps;
        
        if (pnl_bps < 0) {
            loss_streak_++;
        } else {
            loss_streak_ = 0;
        }
    }
    
    // ==========================================================================
    // LATENCY-WEIGHTED TP
    // ==========================================================================
    double adjustedTP(double base_tp_bps, double latency_ms) const {
        double tp_mult = 1.0;
        if (latency_ms < 0.5) {
            tp_mult = 1.25;       // Ultra-fast
        } else if (latency_ms < 1.0) {
            tp_mult = 1.10;       // Fast
        } else if (latency_ms > 1.5) {
            tp_mult = 0.85;       // Slow
        }
        return base_tp_bps * tp_mult;
    }

protected:
    // Core state
    bool enabled_ = true;
    bool in_position_ = false;
    
    // Position state
    double entry_price_ = 0.0;
    uint64_t entry_ts_ = 0;
    bool entry_is_long_ = true;
    
    // Telemetry
    int trades_today_ = 0;
    int loss_streak_ = 0;
    double pnl_today_bps_ = 0.0;
    uint64_t ticks_processed_ = 0;
    uint64_t last_trade_ts_ = 0;
    double last_latency_ms_ = 0.0;
    
    // Callbacks
    OrderCallback order_cb_;
    TradeCallback trade_cb_;
    
    // Debug logging interval
    static constexpr uint64_t DEBUG_LOG_INTERVAL = 500;
};

} // namespace MicroScalp
} // namespace Chimera
