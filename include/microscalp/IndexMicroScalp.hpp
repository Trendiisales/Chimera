#pragma once
// =============================================================================
// IndexMicroScalp.hpp - Index CFD Micro-Scalping Engine (NAS100 / US30)
// =============================================================================
// PHILOSOPHY:
//   - Impulse-only entry (burst continuation)
//   - NY session (RTH) only initially
//   - TAKER-only routing (speed matters)
//   - Time-based exits dominate
//   - Index-specific microstructure
//
// WHY THESE WORK:
//   NAS100: Deep book, frequent micro-bursts, tech flow
//   US30:   Strong impulse moves, clean liquidity reaction
//
// WHY NOT OTHERS:
//   SPX500: Slower, wider spreads
//   DAX:    Spiky but poor fill predictability
//
// v4.9.3: Initial implementation
// =============================================================================

#include "MicroScalpBase.hpp"
#include <string>
#include <cstring>

namespace Chimera {
namespace MicroScalp {

// =============================================================================
// Index Symbol Types
// =============================================================================
enum class IndexSymbol : uint8_t {
    NAS100 = 0,
    US30   = 1,
    OTHER  = 255
};

inline IndexSymbol parseIndexSymbol(const char* sym) {
    if (std::strcmp(sym, "NAS100") == 0 || std::strcmp(sym, "USTEC") == 0) return IndexSymbol::NAS100;
    if (std::strcmp(sym, "US30") == 0 || std::strcmp(sym, "USDOW") == 0) return IndexSymbol::US30;
    return IndexSymbol::OTHER;
}

inline const char* indexSymbolStr(IndexSymbol s) {
    switch (s) {
        case IndexSymbol::NAS100: return "NAS100";
        case IndexSymbol::US30:   return "US30";
        default: return "OTHER";
    }
}

// =============================================================================
// IndexMicroScalp Engine
// =============================================================================
class IndexMicroScalpEngine : public MicroScalpBase {
public:
    explicit IndexMicroScalpEngine(const std::string& symbol);
    
    void onTick(const BaseTick& tick) override;
    const char* engineName() const override { return "IndexMicroScalp"; }
    const char* symbol() const override { return symbol_.c_str(); }
    
    // Config
    void setBaseQty(double q) { base_qty_ = q; }
    
private:
    void tryEnter(const BaseTick& tick);
    void manageExit(const BaseTick& tick);
    
    double calcEdgeBps(const BaseTick& tick) const;
    bool checkEntryConditions(const BaseTick& tick) const;
    
    // Symbol-specific parameters
    double entryEdgeBps() const;
    double takeProfitBps() const;
    double stopLossBps() const;
    uint64_t maxHoldNs() const;
    double maxSpreadBps() const;
    double dailyLossCapBps() const;
    int maxLossStreak() const;
    double sizeMultiplier() const;
    
    void openPosition(const BaseTick& tick, bool is_long, double edge_bps);
    void closePosition(const BaseTick& tick, const char* reason, double pnl_bps);

private:
    std::string symbol_;
    IndexSymbol symbol_type_;
    double base_qty_ = 0.01;  // Minimum lot
    
    // Debug
    uint64_t debug_counter_ = 0;
};

// =============================================================================
// IMPLEMENTATION
// =============================================================================

inline IndexMicroScalpEngine::IndexMicroScalpEngine(const std::string& symbol)
    : symbol_(symbol)
    , symbol_type_(parseIndexSymbol(symbol.c_str()))
{
    if (symbol_.empty()) {
        printf("[INDEX-MS][FATAL] Empty symbol passed to constructor!\n");
    }
    printf("[INDEX-MS] Created engine for %s (type=%d)\n", 
           symbol_.c_str(), static_cast<int>(symbol_type_));
}

inline void IndexMicroScalpEngine::onTick(const BaseTick& tick) {
    // Global tick counter for wiring proof
    static std::atomic<uint64_t> __idx_global_ticks{0};
    uint64_t gticks = __idx_global_ticks.fetch_add(1, std::memory_order_relaxed);
    if ((gticks % 1000) == 0) {
        printf("[INDEX-MS][ASSERT] global_ticks=%llu symbol=%s\n",
               static_cast<unsigned long long>(gticks), symbol_.c_str());
    }
    
    if (!enabled_) return;
    
    ticks_processed_++;
    debug_counter_++;
    last_latency_ms_ = tick.latency_ms;
    
    // Debug logging every N ticks
    if (debug_counter_ % DEBUG_LOG_INTERVAL == 0) {
        printf("[INDEX-MS][%s] t=%llu edge=%.2fbps ofi=%.3f mom=%.3f spread=%.2fbps regime=%s session=%s pos=%s\n",
               symbol_.c_str(),
               static_cast<unsigned long long>(ticks_processed_),
               calcEdgeBps(tick),
               tick.ofi,
               tick.momentum,
               tick.spread_bps,
               tick.regime_impulse ? "IMPULSE" : "QUIET",
               tick.session_ny ? "NY" : "OFF",
               in_position_ ? "OPEN" : "FLAT");
    }
    
    if (in_position_) {
        manageExit(tick);
    } else {
        tryEnter(tick);
    }
}

inline void IndexMicroScalpEngine::tryEnter(const BaseTick& tick) {
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
    
    // Direction from OFI
    bool is_long = (tick.ofi > 0);
    openPosition(tick, is_long, edge);
}

inline bool IndexMicroScalpEngine::checkEntryConditions(const BaseTick& tick) const {
    // 1. NY session only (RTH)
    if (!tick.session_ny) return false;
    
    // 2. Regime must be IMPULSE or BREAKOUT
    if (!tick.regime_impulse) return false;
    
    // 3. Spread check
    if (!spreadOK(tick.spread_bps, maxSpreadBps())) return false;
    
    // 4. Latency check
    if (!latencyOK(2.0)) return false;  // Max 2ms for indices
    
    // 5. OFI magnitude threshold
    if (std::fabs(tick.ofi) < 0.25) return false;
    
    return true;
}

// =============================================================================
// Index Edge Calculation
// =============================================================================
// edge_bps = |ofi| * 1.5 + (pressure_aligned ? 0.4 : 0.0) + impulse_bonus
// NO volatility multiplier - indices overreact to vol
// =============================================================================
inline double IndexMicroScalpEngine::calcEdgeBps(const BaseTick& tick) const {
    // Base OFI edge
    double ofi_edge = std::fabs(tick.ofi) * 1.5;
    
    // Pressure alignment bonus
    double pressure_bonus = tick.pressure_aligned ? 0.4 : 0.0;
    
    // Impulse bonus - reward burst continuation
    double impulse_bonus = 0.0;
    if (tick.regime_impulse && std::fabs(tick.momentum) > 0.5) {
        impulse_bonus = 0.3;
    }
    
    return ofi_edge + pressure_bonus + impulse_bonus;
}

// =============================================================================
// Symbol-Specific Parameters
// =============================================================================

inline double IndexMicroScalpEngine::entryEdgeBps() const {
    switch (symbol_type_) {
        case IndexSymbol::NAS100: return 0.35;  // Tighter (more liquid)
        case IndexSymbol::US30:   return 0.45;  // Wider (less liquid)
        default: return 0.40;
    }
}

inline double IndexMicroScalpEngine::takeProfitBps() const {
    switch (symbol_type_) {
        case IndexSymbol::NAS100: return 0.9;
        case IndexSymbol::US30:   return 1.2;
        default: return 1.0;
    }
}

inline double IndexMicroScalpEngine::stopLossBps() const {
    switch (symbol_type_) {
        case IndexSymbol::NAS100: return 0.6;
        case IndexSymbol::US30:   return 0.8;
        default: return 0.7;
    }
}

inline uint64_t IndexMicroScalpEngine::maxHoldNs() const {
    switch (symbol_type_) {
        case IndexSymbol::NAS100: return 600'000'000ULL;   // 600ms
        case IndexSymbol::US30:   return 800'000'000ULL;   // 800ms
        default: return 700'000'000ULL;
    }
}

inline double IndexMicroScalpEngine::maxSpreadBps() const {
    switch (symbol_type_) {
        case IndexSymbol::NAS100: return 1.2;
        case IndexSymbol::US30:   return 1.5;
        default: return 1.4;
    }
}

inline double IndexMicroScalpEngine::dailyLossCapBps() const {
    switch (symbol_type_) {
        case IndexSymbol::NAS100: return -30.0;  // -0.30%
        case IndexSymbol::US30:   return -35.0;  // -0.35%
        default: return -30.0;
    }
}

inline int IndexMicroScalpEngine::maxLossStreak() const {
    return 2;  // Tight - stop after 2 losses
}

inline double IndexMicroScalpEngine::sizeMultiplier() const {
    switch (symbol_type_) {
        case IndexSymbol::NAS100: return 0.4;
        case IndexSymbol::US30:   return 0.3;
        default: return 0.35;
    }
}

// =============================================================================
// Position Management
// =============================================================================

inline void IndexMicroScalpEngine::manageExit(const BaseTick& tick) {
    uint64_t age_ns = tick.ts_ns - entry_ts_;
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
    
    // Time stop - DOMINANT for indices
    if (age_ns >= max_hold) {
        closePosition(tick, "TIME", pnl_bps);
        return;
    }
}

inline void IndexMicroScalpEngine::openPosition(const BaseTick& tick, bool is_long, double edge_bps) {
    double qty = base_qty_ * sizeMultiplier();
    
    // Zero-qty guard
    if (qty <= 0.0) {
        printf("[INDEX-MS][%s] BLOCKED: Zero quantity\n", symbol_.c_str());
        return;
    }
    
    // Send TAKER order (market)
    if (order_cb_) {
        order_cb_(symbol_.c_str(), is_long, qty, false);  // false = taker
    }
    
    in_position_ = true;
    entry_is_long_ = is_long;
    entry_price_ = is_long ? tick.ask : tick.bid;
    entry_ts_ = tick.ts_ns;
    last_trade_ts_ = tick.ts_ns;
    
    if (trade_cb_) {
        trade_cb_(symbol_.c_str(), engineName(), is_long ? +1 : -1, qty, entry_price_, 0.0);
    }
    
    printf("[INDEX-MS][%s] ENTER %s @ %.2f qty=%.4f edge=%.3fbps spread=%.2fbps\n",
           symbol_.c_str(),
           is_long ? "LONG" : "SHORT",
           entry_price_,
           qty,
           edge_bps,
           tick.spread_bps);
}

inline void IndexMicroScalpEngine::closePosition(const BaseTick& tick, const char* reason, double pnl_bps) {
    double qty = base_qty_ * sizeMultiplier();
    double exit_price = entry_is_long_ ? tick.bid : tick.ask;
    
    // Zero-qty guard
    if (qty <= 0.0) {
        printf("[INDEX-MS][%s] EXIT BLOCKED: Zero quantity\n", symbol_.c_str());
        in_position_ = false;
        return;
    }
    
    // Send TAKER exit
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
    printf("[INDEX-MS][%s] EXIT %s @ %.2f pnl=%.2fbps reason=%s hold=%llums\n",
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
