// ═══════════════════════════════════════════════════════════════════════════════
// include/gui/TradeDecision.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.12: COMPLETE TRADE DECISION PIPELINE
//
// PURPOSE: Encapsulate the entire trade decision with full context.
// Every gate in the system contributes to this decision.
//
// DECISION FLOW:
//   News Gate         → Can we trade at all?
//   Physics Gate      → What execution modes are allowed?
//   Regime Detection  → What kind of market is this?
//   Alpha Selection   → Which idea works here?
//   Symbol Ranking    → Where should capital go?
//   Session Weights   → Is now a good time?
//   Risk Profile      → How big should we trade?
//   → FINAL DECISION
//
// CRITICAL: The FIRST blocking gate sets the NoTradeReason.
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include "NoTradeReason.hpp"
#include "../alpha/MarketRegime.hpp"
#include "../alpha/AlphaSelector.hpp"
#include "../execution/SessionWeights.hpp"
#include <cstdint>

namespace Chimera {
namespace GUI {

// ─────────────────────────────────────────────────────────────────────────────
// Trade Decision - The Final Word
// ─────────────────────────────────────────────────────────────────────────────
struct TradeDecision {
    // Core decision
    bool allowed = false;
    NoTradeReason reason = NoTradeReason::CONNECTED_WAITING;
    
    // Context at decision time
    Alpha::MarketRegime regime = Alpha::MarketRegime::DEAD;
    Alpha::AlphaType alpha = Alpha::AlphaType::NONE;
    Execution::TradingSession session = Execution::TradingSession::OFF_HOURS;
    
    // Sizing (if allowed)
    double size_multiplier = 1.0;
    double edge_adjustment = 1.0;
    
    // Metrics at decision time
    double expectancy = 0.0;
    double latency_p95_ms = 0.0;
    double spread_bps = 0.0;
    
    // Timing
    uint64_t timestamp_ns = 0;
    uint64_t block_duration_ns = 0;
    
    // Symbol
    char symbol[16] = {0};
    
    // Human-readable explanation
    const char* explanation = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// Decision Gate - Individual gate check result
// ─────────────────────────────────────────────────────────────────────────────
struct GateResult {
    bool passed = true;
    NoTradeReason block_reason = NoTradeReason::NONE;
    const char* detail = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// Decision Context - All inputs to decision
// ─────────────────────────────────────────────────────────────────────────────
struct DecisionContext {
    // Symbol
    char symbol[16] = {0};
    
    // Market state
    Alpha::MarketSnapshot snapshot;
    Alpha::MarketRegime regime = Alpha::MarketRegime::DEAD;
    
    // Execution state
    bool in_bootstrap = false;
    double latency_p95_ms = 0.0;
    double spread_bps = 0.0;
    double fill_rate = 0.0;
    double reject_rate = 0.0;
    
    // Risk state
    bool risk_allows = true;
    bool global_kill = false;
    bool daily_loss_hit = false;
    double drawdown_pct = 0.0;
    double governor_heat = 0.0;
    
    // Session/timing
    int utc_hour = 12;
    double session_weight = 1.0;
    
    // Symbol state
    double symbol_expectancy = 0.0;
    bool symbol_enabled = true;
    
    // News
    bool news_halt_active = false;
    
    // Position
    int current_positions = 0;
    int max_positions = 3;
    
    // Cooldown
    bool cooldown_active = false;
    
    // Timestamp
    uint64_t now_ns = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Decision Builder - Runs all gates in order
// ─────────────────────────────────────────────────────────────────────────────
class DecisionBuilder {
public:
    // Run all gates and return final decision
    static TradeDecision evaluate(const DecisionContext& ctx) {
        TradeDecision dec;
        strncpy(dec.symbol, ctx.symbol, 15);
        dec.timestamp_ns = ctx.now_ns;
        dec.regime = ctx.regime;
        dec.session = Execution::detectSession(ctx.utc_hour);
        dec.latency_p95_ms = ctx.latency_p95_ms;
        dec.spread_bps = ctx.spread_bps;
        
        // ─────────────────────────────────────────────────────────────────────
        // Gate 0: Global Kill
        // ─────────────────────────────────────────────────────────────────────
        if (ctx.global_kill) {
            dec.reason = NoTradeReason::GLOBAL_KILL;
            dec.explanation = "Kill switch activated";
            return dec;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Gate 1: Daily Loss
        // ─────────────────────────────────────────────────────────────────────
        if (ctx.daily_loss_hit) {
            dec.reason = NoTradeReason::DAILY_LOSS_CAP;
            dec.explanation = "Daily loss limit reached";
            return dec;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Gate 2: Drawdown Limit
        // ─────────────────────────────────────────────────────────────────────
        if (ctx.drawdown_pct > 4.0) {  // 4% drawdown = full stop
            dec.reason = NoTradeReason::DRAWDOWN_LIMIT;
            dec.explanation = "Drawdown protection triggered";
            return dec;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Gate 3: System Bootstrap
        // ─────────────────────────────────────────────────────────────────────
        if (ctx.in_bootstrap) {
            dec.reason = NoTradeReason::SYSTEM_BOOTSTRAP;
            dec.explanation = "System measuring latency";
            return dec;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Gate 4: News Halt
        // ─────────────────────────────────────────────────────────────────────
        if (ctx.news_halt_active) {
            dec.reason = NoTradeReason::NEWS_HALT;
            dec.explanation = "Hard halt around news event";
            return dec;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Gate 5: Symbol Enabled
        // ─────────────────────────────────────────────────────────────────────
        if (!ctx.symbol_enabled) {
            dec.reason = NoTradeReason::SYMBOL_DISABLED;
            dec.explanation = "Symbol pruned from rotation";
            return dec;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Gate 6: Regime Detection
        // ─────────────────────────────────────────────────────────────────────
        if (ctx.regime == Alpha::MarketRegime::DEAD) {
            dec.reason = NoTradeReason::REGIME_DEAD;
            dec.explanation = "No market structure";
            return dec;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Gate 7: Alpha Selection & Validation
        // ─────────────────────────────────────────────────────────────────────
        Alpha::AlphaType alpha = Alpha::selectAlpha(ctx.regime);
        dec.alpha = alpha;
        
        if (alpha == Alpha::AlphaType::NONE) {
            dec.reason = NoTradeReason::ALPHA_NOT_VALID;
            dec.explanation = "No alpha for this regime";
            return dec;
        }
        
        // Check alpha is active (not retired)
        if (!Alpha::getAlphaRegistry().isActive(alpha)) {
            dec.reason = NoTradeReason::ALPHA_RETIRED;
            dec.explanation = "Alpha auto-retired due to poor metrics";
            return dec;
        }
        
        // Validate alpha for current conditions
        if (!Alpha::isAlphaValid(alpha, ctx.snapshot)) {
            dec.reason = NoTradeReason::ALPHA_NOT_VALID;
            dec.explanation = "Alpha conditions not satisfied";
            return dec;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Gate 8: Symbol Expectancy
        // ─────────────────────────────────────────────────────────────────────
        if (ctx.symbol_expectancy < 0.0) {
            dec.reason = NoTradeReason::SYMBOL_EXPECTANCY_NEG;
            dec.expectancy = ctx.symbol_expectancy;
            dec.explanation = "Negative symbol expectancy";
            return dec;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Gate 9: Session Expectancy
        // ─────────────────────────────────────────────────────────────────────
        if (ctx.session_weight < 0.5) {
            dec.reason = NoTradeReason::SESSION_LOW_EXPECTANCY;
            dec.explanation = "Session expectancy too low";
            return dec;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Gate 10: Latency Degradation
        // ─────────────────────────────────────────────────────────────────────
        if (ctx.latency_p95_ms > 50.0) {  // 50ms = severely degraded
            dec.reason = NoTradeReason::LATENCY_DEGRADED;
            dec.explanation = "Hot-path latency degraded";
            return dec;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Gate 11: Spread Check
        // ─────────────────────────────────────────────────────────────────────
        if (ctx.spread_bps > 20.0) {  // 20 bps = too wide
            dec.reason = NoTradeReason::SPREAD_TOO_WIDE;
            dec.explanation = "Spread exceeds threshold";
            return dec;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Gate 12: Position Limit
        // ─────────────────────────────────────────────────────────────────────
        if (ctx.current_positions >= ctx.max_positions) {
            dec.reason = NoTradeReason::POSITION_LIMIT;
            dec.explanation = "Maximum positions reached";
            return dec;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Gate 13: Cooldown
        // ─────────────────────────────────────────────────────────────────────
        if (ctx.cooldown_active) {
            dec.reason = NoTradeReason::COOLDOWN_ACTIVE;
            dec.explanation = "Post-trade cooldown";
            return dec;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Gate 14: Risk Allows
        // ─────────────────────────────────────────────────────────────────────
        if (!ctx.risk_allows) {
            dec.reason = NoTradeReason::RISK_BACKOFF;
            dec.explanation = "Risk governor blocking";
            return dec;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Gate 15: Governor Heat
        // ─────────────────────────────────────────────────────────────────────
        if (ctx.governor_heat > 0.9) {
            dec.reason = NoTradeReason::GOVERNOR_HEAT;
            dec.explanation = "Governor heat too high";
            return dec;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // ALL GATES PASSED - Trade is allowed
        // ─────────────────────────────────────────────────────────────────────
        dec.allowed = true;
        dec.reason = NoTradeReason::NONE;
        dec.expectancy = ctx.symbol_expectancy;
        
        // Calculate size multiplier based on conditions
        dec.size_multiplier = calculateSizeMultiplier(ctx);
        dec.edge_adjustment = ctx.session_weight;
        dec.explanation = "Trade allowed";
        
        return dec;
    }
    
private:
    static double calculateSizeMultiplier(const DecisionContext& ctx) {
        double mult = 1.0;
        
        // Session weight
        mult *= ctx.session_weight;
        
        // Governor heat reduces size
        if (ctx.governor_heat > 0.3) {
            mult *= (1.0 - ctx.governor_heat * 0.5);
        }
        
        // Drawdown reduces size
        if (ctx.drawdown_pct > 1.0) {
            mult *= std::max(0.3, 1.0 - ctx.drawdown_pct * 0.15);
        }
        
        // Clamp
        return std::clamp(mult, 0.2, 1.5);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Quick Decision Check (for hot path)
// ─────────────────────────────────────────────────────────────────────────────
inline bool canTradeQuick(
    bool global_kill,
    bool daily_loss_hit,
    bool in_bootstrap,
    Alpha::MarketRegime regime
) {
    if (global_kill) return false;
    if (daily_loss_hit) return false;
    if (in_bootstrap) return false;
    if (regime == Alpha::MarketRegime::DEAD) return false;
    return true;
}

} // namespace GUI
} // namespace Chimera
