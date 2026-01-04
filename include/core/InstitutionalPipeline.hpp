// ═══════════════════════════════════════════════════════════════════════════════
// include/core/InstitutionalPipeline.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.12: THE COMPLETE INSTITUTIONAL DECISION PIPELINE
//
// PURPOSE: Single entry point for all trade decisions.
// Every question has exactly one answerer:
//
// Question                          Answered By
// ─────────────────────────────────────────────────────────
// Can we trade at all?              News + Physics Gates
// What kind of market is this?      Regime Detector
// Which idea works here?            Alpha Selector
// Where should capital go?          Symbol Selector
// Is now a good time?               Session Expectancy
// How big should we trade?          Regime Risk Profile
// Why didn't we trade?              NoTrade Reason (GUI)
//
// THIS IS FULL DECISION COMPLETENESS.
//
// FLOW:
// Tick/Event
//   ↓
// Execution Physics (WAN / COLO)
//   ↓
// News Gate (can we trade at all?)
//   ↓
// Regime Detection (TREND / RANGE / VOL / DEAD)
//   ↓
// Alpha Module Selection
//   ↓
// Symbol Expectancy Ranking
//   ↓
// Session Expectancy Scaling
//   ↓
// Risk Profile Selection
//   ↓
// Execution Feasibility (existing system)
//   ↓
// ORDER (or NO_TRADE with reason)
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

// Core includes
#include "../alpha/MarketRegime.hpp"
#include "../alpha/AlphaSelector.hpp"
#include "../alpha/SessionExpectancy.hpp"
#include "../gui/NoTradeReason.hpp"
#include "../gui/TradeDecision.hpp"
#include "../system/NewsGate.hpp"
#include "../risk/RegimeRiskProfile.hpp"
#include "../symbol/SymbolSelector.hpp"
#include "../audit/RegimePnL.hpp"
#include "../execution/SessionWeights.hpp"
#include "../runtime/SystemMode.hpp"

#include <cstdint>
#include <chrono>

namespace Chimera {
namespace Core {

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline Input - All data needed for decision
// ─────────────────────────────────────────────────────────────────────────────
struct PipelineInput {
    // Symbol
    char symbol[16] = {0};
    
    // Market data
    Alpha::MarketSnapshot snapshot;
    double current_price = 0.0;
    double spread_bps = 0.0;
    
    // Execution metrics
    double latency_p95_ms = 0.0;
    double fill_rate = 0.0;
    double reject_rate = 0.0;
    
    // Risk state
    double current_drawdown_pct = 0.0;
    double daily_pnl_pct = 0.0;
    int consecutive_losses = 0;
    int consecutive_wins = 0;
    double governor_heat = 0.0;
    int positions_open = 0;
    int max_positions = 3;
    
    // Global state
    bool global_kill = false;
    bool daily_loss_hit = false;
    bool risk_allows = true;
    bool in_bootstrap = false;
    bool cooldown_active = false;
    
    // Signal
    bool has_signal = false;
    int8_t signal_side = 0;            // 1 = long, -1 = short
    double signal_conviction = 0.0;
    double base_size = 0.0;
    double base_stop_bps = 0.0;
    
    // Timing
    uint64_t now_ns = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline Output - Complete decision with context
// ─────────────────────────────────────────────────────────────────────────────
struct PipelineOutput {
    // Decision
    bool trade_allowed = false;
    GUI::NoTradeReason no_trade_reason = GUI::NoTradeReason::CONNECTED_WAITING;
    
    // Context at decision
    Alpha::MarketRegime regime = Alpha::MarketRegime::DEAD;
    Alpha::AlphaType alpha = Alpha::AlphaType::NONE;
    Execution::TradingSession session = Execution::TradingSession::OFF_HOURS;
    
    // Adjusted parameters
    double size_multiplier = 1.0;
    double final_size = 0.0;
    double edge_adjustment = 1.0;
    double stop_multiplier = 1.0;
    double target_multiplier = 1.0;
    
    // Risk profile
    Risk::RiskProfile risk_profile;
    Risk::RiskAdjustment risk_adjustment;
    
    // Metrics
    double symbol_expectancy = 0.0;
    double session_weight = 1.0;
    double symbol_score = 0.0;
    
    // Explanation
    const char* explanation = nullptr;
    
    // Timing
    uint64_t decision_ns = 0;
    uint64_t pipeline_latency_ns = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Institutional Pipeline - The Master Orchestrator
// ─────────────────────────────────────────────────────────────────────────────
class InstitutionalPipeline {
public:
    // ═══════════════════════════════════════════════════════════════════════
    // MAIN ENTRY POINT
    // ═══════════════════════════════════════════════════════════════════════
    PipelineOutput evaluate(const PipelineInput& input) {
        uint64_t start_ns = nowNs();
        PipelineOutput output;
        output.decision_ns = start_ns;
        
        // ─────────────────────────────────────────────────────────────────
        // GATE 0: Global Kill
        // ─────────────────────────────────────────────────────────────────
        if (input.global_kill) {
            output.no_trade_reason = GUI::NoTradeReason::GLOBAL_KILL;
            output.explanation = "Kill switch activated";
            return finalize(output, start_ns);
        }
        
        // ─────────────────────────────────────────────────────────────────
        // GATE 1: Daily Loss Cap
        // ─────────────────────────────────────────────────────────────────
        if (input.daily_loss_hit) {
            output.no_trade_reason = GUI::NoTradeReason::DAILY_LOSS_CAP;
            output.explanation = "Daily loss limit reached";
            return finalize(output, start_ns);
        }
        
        // ─────────────────────────────────────────────────────────────────
        // GATE 2: Drawdown Limit
        // ─────────────────────────────────────────────────────────────────
        if (input.current_drawdown_pct > 4.0) {
            output.no_trade_reason = GUI::NoTradeReason::DRAWDOWN_LIMIT;
            output.explanation = "Drawdown protection triggered";
            return finalize(output, start_ns);
        }
        
        // ─────────────────────────────────────────────────────────────────
        // GATE 3: System Bootstrap
        // ─────────────────────────────────────────────────────────────────
        if (input.in_bootstrap) {
            output.no_trade_reason = GUI::NoTradeReason::SYSTEM_BOOTSTRAP;
            output.explanation = "System measuring latency";
            return finalize(output, start_ns);
        }
        
        // ─────────────────────────────────────────────────────────────────
        // GATE 4: News Halt
        // ─────────────────────────────────────────────────────────────────
        if (System::getNewsGate().isHaltActive()) {
            output.no_trade_reason = GUI::NoTradeReason::NEWS_HALT;
            output.explanation = "Hard halt around news event";
            return finalize(output, start_ns);
        }
        
        // ─────────────────────────────────────────────────────────────────
        // DETECT REGIME
        // ─────────────────────────────────────────────────────────────────
        auto& regime_mgr = Alpha::getSymbolRegimeManager();
        auto* regime_detector = regime_mgr.get(input.symbol);
        Alpha::RegimeResult regime_result;
        
        if (regime_detector) {
            regime_result = regime_detector->detect(input.snapshot);
        } else {
            regime_result.regime = Alpha::RegimeDetector::quickClassify(input.snapshot);
        }
        
        output.regime = regime_result.regime;
        
        // ─────────────────────────────────────────────────────────────────
        // GATE 5: Regime Dead
        // ─────────────────────────────────────────────────────────────────
        if (output.regime == Alpha::MarketRegime::DEAD) {
            output.no_trade_reason = GUI::NoTradeReason::REGIME_DEAD;
            output.explanation = regime_result.reason;
            return finalize(output, start_ns);
        }
        
        // ─────────────────────────────────────────────────────────────────
        // SELECT ALPHA
        // ─────────────────────────────────────────────────────────────────
        output.alpha = Alpha::selectAlpha(output.regime);
        
        // ─────────────────────────────────────────────────────────────────
        // GATE 6: Alpha Retired
        // ─────────────────────────────────────────────────────────────────
        if (!Alpha::getAlphaRegistry().isActive(output.alpha)) {
            output.no_trade_reason = GUI::NoTradeReason::ALPHA_RETIRED;
            output.explanation = "Alpha auto-retired";
            return finalize(output, start_ns);
        }
        
        // ─────────────────────────────────────────────────────────────────
        // GATE 7: Alpha Validity
        // ─────────────────────────────────────────────────────────────────
        if (!Alpha::isAlphaValid(output.alpha, input.snapshot)) {
            output.no_trade_reason = GUI::NoTradeReason::ALPHA_NOT_VALID;
            output.explanation = "Alpha conditions not met";
            return finalize(output, start_ns);
        }
        
        // ─────────────────────────────────────────────────────────────────
        // SESSION DETECTION
        // ─────────────────────────────────────────────────────────────────
        int utc_hour = Execution::getUtcHour();
        output.session = Execution::detectSession(utc_hour);
        
        // ─────────────────────────────────────────────────────────────────
        // SESSION EXPECTANCY
        // ─────────────────────────────────────────────────────────────────
        output.session_weight = Alpha::getSessionExpectancyManager()
                                     .getEdgeMultiplier(input.symbol, utc_hour);
        
        // ─────────────────────────────────────────────────────────────────
        // GATE 8: Session Low Expectancy
        // ─────────────────────────────────────────────────────────────────
        if (output.session_weight < 0.4) {
            output.no_trade_reason = GUI::NoTradeReason::SESSION_LOW_EXPECTANCY;
            output.explanation = "Session expectancy too low";
            return finalize(output, start_ns);
        }
        
        // ─────────────────────────────────────────────────────────────────
        // SYMBOL SELECTION
        // ─────────────────────────────────────────────────────────────────
        auto& selector = Symbol::getSymbolSelector();
        const Symbol::SymbolScore* score = selector.getScore(input.symbol);
        
        if (score) {
            output.symbol_expectancy = score->expectancy;
            output.symbol_score = Symbol::computeSymbolScore(*score);
        }
        
        // ─────────────────────────────────────────────────────────────────
        // GATE 9: Symbol Not Active (rotated out)
        // ─────────────────────────────────────────────────────────────────
        if (score && !score->enabled) {
            output.no_trade_reason = GUI::NoTradeReason::SYMBOL_DISABLED;
            output.explanation = "Symbol pruned from rotation";
            return finalize(output, start_ns);
        }
        
        // ─────────────────────────────────────────────────────────────────
        // GATE 10: Symbol Negative Expectancy
        // ─────────────────────────────────────────────────────────────────
        if (score && score->expectancy < -0.05) {
            output.no_trade_reason = GUI::NoTradeReason::SYMBOL_EXPECTANCY_NEG;
            output.explanation = "Symbol expectancy negative";
            return finalize(output, start_ns);
        }
        
        // ─────────────────────────────────────────────────────────────────
        // GATE 11: Latency Degradation
        // ─────────────────────────────────────────────────────────────────
        if (input.latency_p95_ms > 50.0) {
            output.no_trade_reason = GUI::NoTradeReason::LATENCY_DEGRADED;
            output.explanation = "Hot-path latency degraded";
            return finalize(output, start_ns);
        }
        
        // ─────────────────────────────────────────────────────────────────
        // GATE 12: Spread Check
        // ─────────────────────────────────────────────────────────────────
        if (input.spread_bps > 20.0) {
            output.no_trade_reason = GUI::NoTradeReason::SPREAD_TOO_WIDE;
            output.explanation = "Spread exceeds threshold";
            return finalize(output, start_ns);
        }
        
        // ─────────────────────────────────────────────────────────────────
        // GATE 13: Position Limit
        // ─────────────────────────────────────────────────────────────────
        if (input.positions_open >= input.max_positions) {
            output.no_trade_reason = GUI::NoTradeReason::POSITION_LIMIT;
            output.explanation = "Maximum positions reached";
            return finalize(output, start_ns);
        }
        
        // ─────────────────────────────────────────────────────────────────
        // GATE 14: Cooldown
        // ─────────────────────────────────────────────────────────────────
        if (input.cooldown_active) {
            output.no_trade_reason = GUI::NoTradeReason::COOLDOWN_ACTIVE;
            output.explanation = "Post-trade cooldown";
            return finalize(output, start_ns);
        }
        
        // ─────────────────────────────────────────────────────────────────
        // GATE 15: Risk Allows
        // ─────────────────────────────────────────────────────────────────
        if (!input.risk_allows) {
            output.no_trade_reason = GUI::NoTradeReason::RISK_BACKOFF;
            output.explanation = "Risk governor blocking";
            return finalize(output, start_ns);
        }
        
        // ─────────────────────────────────────────────────────────────────
        // GATE 16: Governor Heat
        // ─────────────────────────────────────────────────────────────────
        if (input.governor_heat > 0.9) {
            output.no_trade_reason = GUI::NoTradeReason::GOVERNOR_HEAT;
            output.explanation = "Governor heat maximum";
            return finalize(output, start_ns);
        }
        
        // ─────────────────────────────────────────────────────────────────
        // GATE 17: No Signal
        // ─────────────────────────────────────────────────────────────────
        if (!input.has_signal) {
            output.no_trade_reason = GUI::NoTradeReason::WAITING_FOR_SIGNAL;
            output.explanation = "Waiting for trade signal";
            return finalize(output, start_ns);
        }
        
        // ═══════════════════════════════════════════════════════════════════
        // ALL GATES PASSED - CALCULATE SIZING
        // ═══════════════════════════════════════════════════════════════════
        
        // Get regime risk profile
        output.risk_profile = Risk::riskForRegime(output.regime);
        
        // Build risk context
        Risk::RiskContext risk_ctx;
        risk_ctx.regime = output.regime;
        risk_ctx.current_drawdown_pct = input.current_drawdown_pct;
        risk_ctx.daily_pnl_pct = input.daily_pnl_pct;
        risk_ctx.consecutive_losses = input.consecutive_losses;
        risk_ctx.consecutive_wins = input.consecutive_wins;
        risk_ctx.governor_heat = input.governor_heat;
        risk_ctx.positions_open = input.positions_open;
        risk_ctx.max_positions = input.max_positions;
        
        // Calculate risk adjustment
        output.risk_adjustment = Risk::calculateRiskAdjustment(risk_ctx);
        
        // Final multipliers
        output.size_multiplier = output.risk_adjustment.size_mult * 
                                 output.session_weight *
                                 output.risk_profile.size_multiplier;
        
        output.stop_multiplier = output.risk_adjustment.stop_mult * 
                                 output.risk_profile.stop_mult;
        
        output.target_multiplier = output.risk_adjustment.target_mult * 
                                   output.risk_profile.target_mult;
        
        output.edge_adjustment = output.session_weight;
        
        // Calculate final size
        output.final_size = input.base_size * output.size_multiplier;
        
        // Clamp
        output.size_multiplier = std::clamp(output.size_multiplier, 0.2, 2.0);
        output.final_size = std::max(0.0, output.final_size);
        
        // DECISION: TRADE ALLOWED
        output.trade_allowed = output.final_size > 0.0;
        output.no_trade_reason = GUI::NoTradeReason::NONE;
        output.explanation = "Trade approved";
        
        return finalize(output, start_ns);
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // QUICK CHECK (for hot path)
    // ═══════════════════════════════════════════════════════════════════════
    static bool quickCheck(
        bool global_kill,
        bool daily_loss_hit,
        bool in_bootstrap,
        Alpha::MarketRegime regime
    ) {
        if (global_kill) return false;
        if (daily_loss_hit) return false;
        if (in_bootstrap) return false;
        if (regime == Alpha::MarketRegime::DEAD) return false;
        if (System::getNewsGate().isHaltActive()) return false;
        return true;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // UPDATE NO-TRADE STATE FOR GUI
    // ═══════════════════════════════════════════════════════════════════════
    void updateNoTradeState(const char* symbol, const PipelineOutput& output) {
        GUI::getNoTradeStateManager().update(
            symbol, 
            output.no_trade_reason,
            output.decision_ns,
            output.explanation
        );
    }
    
private:
    static uint64_t nowNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }
    
    PipelineOutput finalize(PipelineOutput& output, uint64_t start_ns) {
        output.pipeline_latency_ns = nowNs() - start_ns;
        return output;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Global Pipeline Instance
// ─────────────────────────────────────────────────────────────────────────────
inline InstitutionalPipeline& getInstitutionalPipeline() {
    static InstitutionalPipeline pipeline;
    return pipeline;
}

// ─────────────────────────────────────────────────────────────────────────────
// Record Trade for Attribution (call on trade close)
// ─────────────────────────────────────────────────────────────────────────────
inline void recordTradeAttribution(
    const char* symbol,
    Alpha::MarketRegime regime,
    Alpha::AlphaType alpha,
    int utc_hour,
    double entry_price,
    double exit_price,
    double size,
    int8_t side,
    double fees,
    uint64_t entry_ts,
    uint64_t exit_ts
) {
    // Record in PnL tracker
    Audit::TradeAttribution attr = Audit::createAttribution(
        symbol, regime, alpha, utc_hour,
        entry_price, exit_price, size, side, fees,
        entry_ts, exit_ts
    );
    Audit::getRegimePnLTracker().recordTrade(attr);
    
    // Update alpha registry health
    bool win = attr.net_pnl > 0;
    double pnl_R = attr.net_pnl / (entry_price * 0.001);  // Approximate R
    Alpha::getAlphaRegistry().recordTrade(alpha, win, pnl_R);
    
    // Update session expectancy
    Alpha::getSessionExpectancyManager().recordTrade(symbol, utc_hour, win, pnl_R);
}

} // namespace Core
} // namespace Chimera
