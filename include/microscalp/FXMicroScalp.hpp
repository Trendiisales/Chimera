#pragma once
// =============================================================================
// FXMicroScalp.hpp - FX Micro-Scalping Engine (EURUSD / GBPUSD)
// =============================================================================
// PHILOSOPHY:
//   - Spread capture + mean reversion
//   - MAKER-first routing (capture spread)
//   - Pressure persistence, OFI confirmatory
//   - Latency-aware TP expansion
//   - Ultra-tight risk
//
// WHY FX WORKS:
//   - Deep liquidity, continuous flow
//   - Mean-reversion micro-moves
//   - Predictable spread behavior
//   - Spread compression cycles
//
// WHY ONLY MAJORS:
//   - EURUSD: Deepest, tightest spreads
//   - GBPUSD: Second deepest, good momentum
//   - Exotics: Hidden markups, fragmented liquidity
//
// v4.9.3: Initial implementation
// =============================================================================

#include "MicroScalpBase.hpp"
#include <string>
#include <cstring>

namespace Chimera {
namespace MicroScalp {

// =============================================================================
// FX Symbol Types
// =============================================================================
enum class FXSymbol : uint8_t {
    EURUSD = 0,
    GBPUSD = 1,
    OTHER  = 255
};

inline FXSymbol parseFXSymbol(const char* sym) {
    if (std::strcmp(sym, "EURUSD") == 0) return FXSymbol::EURUSD;
    if (std::strcmp(sym, "GBPUSD") == 0) return FXSymbol::GBPUSD;
    return FXSymbol::OTHER;
}

inline const char* fxSymbolStr(FXSymbol s) {
    switch (s) {
        case FXSymbol::EURUSD: return "EURUSD";
        case FXSymbol::GBPUSD: return "GBPUSD";
        default: return "OTHER";
    }
}

// =============================================================================
// FXMicroScalp Engine
// =============================================================================
class FXMicroScalpEngine : public MicroScalpBase {
public:
    explicit FXMicroScalpEngine(const std::string& symbol);
    
    void onTick(const BaseTick& tick) override;
    const char* engineName() const override { return "FXMicroScalp"; }
    const char* symbol() const override { return symbol_.c_str(); }
    
    // Config
    void setBaseQty(double q) { base_qty_ = q; }
    
private:
    void tryEnter(const BaseTick& tick);
    void manageExit(const BaseTick& tick);
    
    double calcEdgeBps(const BaseTick& tick) const;
    bool checkEntryConditions(const BaseTick& tick) const;
    bool shouldUseMaker(const BaseTick& tick) const;
    
    // Symbol-specific parameters
    double entryEdgeBps() const;
    double takeProfitBps() const;
    double stopLossBps() const;
    uint64_t maxHoldNs() const;
    double maxSpreadBps() const;
    double dailyLossCapBps() const;
    int maxLossStreak() const;
    double sizeMultiplier() const;
    
    void openPosition(const BaseTick& tick, bool is_long, double edge_bps, bool use_maker);
    void closePosition(const BaseTick& tick, const char* reason, double pnl_bps);

private:
    std::string symbol_;
    FXSymbol symbol_type_;
    double base_qty_ = 1000.0;  // Micro lot (0.01 standard)
    
    // Fair value tracking
    double fair_mid_ = 0.0;
    double ema_mid_ = 0.0;
    static constexpr double EMA_ALPHA = 0.05;
    
    // Spread tracking
    double median_spread_bps_ = 0.0;
    double spread_ema_ = 0.0;
    
    // Debug
    uint64_t debug_counter_ = 0;
};

// =============================================================================
// IMPLEMENTATION
// =============================================================================

inline FXMicroScalpEngine::FXMicroScalpEngine(const std::string& symbol)
    : symbol_(symbol)
    , symbol_type_(parseFXSymbol(symbol.c_str()))
{
    if (symbol_.empty()) {
        printf("[FX-MS][FATAL] Empty symbol passed to constructor!\n");
    }
    
    // Initialize median spread estimates
    switch (symbol_type_) {
        case FXSymbol::EURUSD: median_spread_bps_ = 0.3; break;
        case FXSymbol::GBPUSD: median_spread_bps_ = 0.5; break;
        default: median_spread_bps_ = 0.5; break;
    }
    
    printf("[FX-MS] Created engine for %s (type=%d median_spread=%.2fbps)\n", 
           symbol_.c_str(), static_cast<int>(symbol_type_), median_spread_bps_);
}

inline void FXMicroScalpEngine::onTick(const BaseTick& tick) {
    // Global tick counter for wiring proof
    static std::atomic<uint64_t> __fx_global_ticks{0};
    uint64_t gticks = __fx_global_ticks.fetch_add(1, std::memory_order_relaxed);
    if ((gticks % 1000) == 0) {
        printf("[FX-MS][ASSERT] global_ticks=%llu symbol=%s\n",
               static_cast<unsigned long long>(gticks), symbol_.c_str());
    }
    
    if (!enabled_) return;
    
    ticks_processed_++;
    debug_counter_++;
    last_latency_ms_ = tick.latency_ms;
    
    // Update EMA mid for mean reversion
    if (ema_mid_ == 0.0) {
        ema_mid_ = tick.mid;
    } else {
        ema_mid_ = EMA_ALPHA * tick.mid + (1.0 - EMA_ALPHA) * ema_mid_;
    }
    
    // Update spread EMA
    spread_ema_ = EMA_ALPHA * tick.spread_bps + (1.0 - EMA_ALPHA) * spread_ema_;
    
    // Debug logging every N ticks
    if (debug_counter_ % DEBUG_LOG_INTERVAL == 0) {
        printf("[FX-MS][%s] t=%llu edge=%.2fbps ofi=%.3f pres=%.3f spread=%.2fbps comp=%.3f pos=%s\n",
               symbol_.c_str(),
               static_cast<unsigned long long>(ticks_processed_),
               calcEdgeBps(tick),
               tick.ofi,
               tick.pressure,
               tick.spread_bps,
               tick.spread_compression,
               in_position_ ? "OPEN" : "FLAT");
    }
    
    if (in_position_) {
        manageExit(tick);
    } else {
        tryEnter(tick);
    }
}

inline void FXMicroScalpEngine::tryEnter(const BaseTick& tick) {
    // Risk check first
    if (!riskOK(maxLossStreak(), dailyLossCapBps())) {
        return;
    }
    
    // Entry conditions
    if (!checkEntryConditions(tick)) {
        return;
    }
    
    double edge = calcEdgeBps(tick);
    if (edge < entryEdgeBps()) {
        return;
    }
    
    // Direction from pressure (FX is pressure-first)
    bool is_long = (tick.pressure > 0);
    
    // Routing decision
    bool use_maker = shouldUseMaker(tick);
    
    openPosition(tick, is_long, edge, use_maker);
}

inline bool FXMicroScalpEngine::checkEntryConditions(const BaseTick& tick) const {
    // 1. Spread must be ultra-tight
    if (!spreadOK(tick.spread_bps, maxSpreadBps())) return false;
    
    // 2. Pressure persistence required
    if (!tick.pressure_persistent) return false;
    
    // 3. Latency check (tighter for FX)
    if (!latencyOK(1.5)) return false;
    
    // 4. OFI confirms direction (weakly)
    // OFI and pressure should agree
    if ((tick.ofi > 0 && tick.pressure < 0) || 
        (tick.ofi < 0 && tick.pressure > 0)) {
        return false;
    }
    
    return true;
}

// =============================================================================
// FX Edge Calculation
// =============================================================================
// edge_bps = spread_compression * 0.6 + pressure_persistence * 0.5 + |ofi| * 0.8
// OFI is confirmatory, not dominant
// =============================================================================
inline double FXMicroScalpEngine::calcEdgeBps(const BaseTick& tick) const {
    // Spread compression edge
    double spread_edge = tick.spread_compression * 0.6;
    
    // Pressure persistence edge
    double pressure_edge = tick.pressure_persistent ? (std::fabs(tick.pressure) * 0.5) : 0.0;
    
    // OFI confirmation (weakly weighted)
    double ofi_edge = std::fabs(tick.ofi) * 0.8;
    
    return spread_edge + pressure_edge + ofi_edge;
}

// =============================================================================
// Maker/Taker Decision
// =============================================================================
// MAKER if: spread > median AND latency < threshold
// Fallback to TAKER after 150ms in position
// =============================================================================
inline bool FXMicroScalpEngine::shouldUseMaker(const BaseTick& tick) const {
    // Spread wider than median = maker opportunity
    if (tick.spread_bps > median_spread_bps_ && tick.latency_ms < 1.0) {
        return true;  // MAKER
    }
    return false;  // TAKER
}

// =============================================================================
// Symbol-Specific Parameters
// =============================================================================

inline double FXMicroScalpEngine::entryEdgeBps() const {
    switch (symbol_type_) {
        case FXSymbol::EURUSD: return 0.20;  // Tightest
        case FXSymbol::GBPUSD: return 0.25;
        default: return 0.22;
    }
}

inline double FXMicroScalpEngine::takeProfitBps() const {
    switch (symbol_type_) {
        case FXSymbol::EURUSD: return 0.6;
        case FXSymbol::GBPUSD: return 0.8;
        default: return 0.7;
    }
}

inline double FXMicroScalpEngine::stopLossBps() const {
    switch (symbol_type_) {
        case FXSymbol::EURUSD: return 0.4;
        case FXSymbol::GBPUSD: return 0.5;
        default: return 0.45;
    }
}

inline uint64_t FXMicroScalpEngine::maxHoldNs() const {
    switch (symbol_type_) {
        case FXSymbol::EURUSD: return 900'000'000ULL;   // 900ms
        case FXSymbol::GBPUSD: return 900'000'000ULL;   // 900ms
        default: return 900'000'000ULL;
    }
}

inline double FXMicroScalpEngine::maxSpreadBps() const {
    switch (symbol_type_) {
        case FXSymbol::EURUSD: return 0.5;   // Ultra-tight
        case FXSymbol::GBPUSD: return 0.8;
        default: return 0.6;
    }
}

inline double FXMicroScalpEngine::dailyLossCapBps() const {
    switch (symbol_type_) {
        case FXSymbol::EURUSD: return -25.0;  // -0.25%
        case FXSymbol::GBPUSD: return -30.0;  // -0.30%
        default: return -25.0;
    }
}

inline int FXMicroScalpEngine::maxLossStreak() const {
    return 3;  // FX is more stable, allow 3 losses
}

inline double FXMicroScalpEngine::sizeMultiplier() const {
    return 0.4;  // Conservative
}

// =============================================================================
// Position Management
// =============================================================================

inline void FXMicroScalpEngine::manageExit(const BaseTick& tick) {
    uint64_t age_ns = tick.ts_ns - entry_ts_;
    uint64_t age_ms = age_ns / 1'000'000;
    double exit_price = entry_is_long_ ? tick.bid : tick.ask;
    double pnl_bps = entry_is_long_ 
        ? (exit_price - entry_price_) / entry_price_ * 10000.0
        : (entry_price_ - exit_price) / entry_price_ * 10000.0;
    
    double tp = adjustedTP(takeProfitBps(), tick.latency_ms);
    double sl = stopLossBps();
    uint64_t max_hold = maxHoldNs();
    
    // TP hit
    if (pnl_bps >= tp) {
        closePosition(tick, "TP", pnl_bps);
        return;
    }
    
    // SL hit
    if (pnl_bps <= -sl) {
        closePosition(tick, "SL", pnl_bps);
        return;
    }
    
    // Time stop
    if (age_ns >= max_hold) {
        closePosition(tick, "TIME", pnl_bps);
        return;
    }
    
    // Mean reversion exit - if price reverts past fair
    double deviation_bps = (tick.mid - ema_mid_) / ema_mid_ * 10000.0;
    if (entry_is_long_ && deviation_bps > tp * 0.8) {
        closePosition(tick, "REVERT", pnl_bps);
        return;
    }
    if (!entry_is_long_ && deviation_bps < -tp * 0.8) {
        closePosition(tick, "REVERT", pnl_bps);
        return;
    }
}

inline void FXMicroScalpEngine::openPosition(const BaseTick& tick, bool is_long, double edge_bps, bool use_maker) {
    double qty = base_qty_ * sizeMultiplier();
    
    // Zero-qty guard
    if (qty <= 0.0) {
        printf("[FX-MS][%s] BLOCKED: Zero quantity\n", symbol_.c_str());
        return;
    }
    
    // Send order
    if (order_cb_) {
        order_cb_(symbol_.c_str(), is_long, qty, use_maker);
    }
    
    in_position_ = true;
    entry_is_long_ = is_long;
    entry_price_ = is_long ? tick.ask : tick.bid;
    entry_ts_ = tick.ts_ns;
    last_trade_ts_ = tick.ts_ns;
    fair_mid_ = tick.mid;
    
    if (trade_cb_) {
        trade_cb_(symbol_.c_str(), engineName(), is_long ? +1 : -1, qty, entry_price_, 0.0);
    }
    
    printf("[FX-MS][%s] ENTER %s @ %.5f qty=%.0f edge=%.3fbps spread=%.2fbps route=%s\n",
           symbol_.c_str(),
           is_long ? "LONG" : "SHORT",
           entry_price_,
           qty,
           edge_bps,
           tick.spread_bps,
           use_maker ? "MAKER" : "TAKER");
}

inline void FXMicroScalpEngine::closePosition(const BaseTick& tick, const char* reason, double pnl_bps) {
    double qty = base_qty_ * sizeMultiplier();
    double exit_price = entry_is_long_ ? tick.bid : tick.ask;
    
    // Zero-qty guard
    if (qty <= 0.0) {
        printf("[FX-MS][%s] EXIT BLOCKED: Zero quantity\n", symbol_.c_str());
        in_position_ = false;
        return;
    }
    
    // Always exit as TAKER for speed
    if (order_cb_) {
        order_cb_(symbol_.c_str(), !entry_is_long_, qty, false);
    }
    
    // Track stats
    recordTrade(pnl_bps);
    
    // Check kill conditions
    if (loss_streak_ >= maxLossStreak()) {
        disable(KillReason::LOSS_STREAK);
    } else if (pnl_today_bps_ <= dailyLossCapBps()) {
        disable(KillReason::DAILY_CAP);
    }
    
    if (trade_cb_) {
        trade_cb_(symbol_.c_str(), engineName(), entry_is_long_ ? -1 : +1, qty, exit_price, pnl_bps);
    }
    
    uint64_t hold_ms = (tick.ts_ns - entry_ts_) / 1'000'000;
    printf("[FX-MS][%s] EXIT %s @ %.5f pnl=%.2fbps reason=%s hold=%llums\n",
           symbol_.c_str(),
           entry_is_long_ ? "LONG" : "SHORT",
           exit_price,
           pnl_bps,
           reason,
           static_cast<unsigned long long>(hold_ms));
    
    in_position_ = false;
    entry_price_ = 0.0;
    entry_ts_ = 0;
    last_trade_ts_ = tick.ts_ns;
}

} // namespace MicroScalp
} // namespace Chimera
