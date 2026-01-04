// ═══════════════════════════════════════════════════════════════════════════════
// include/alpha/TieredPromotion.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.24: 3-TIER SHADOW PROMOTION MODEL
// STATUS: 🔧 ACTIVE
// OWNER: Jo
// CREATED: 2026-01-03
//
// THE RIGHT DESIGN:
//   - Fail fast
//   - Probe early
//   - Trust late
//
// WHY THIS EXISTS:
//   "50 trades is a lot for discovery but perfect for trust with capital"
//   Solution: Split discovery from trust, don't compromise either.
//
// 3-TIER MODEL:
//
// ┌─────────────────────────────────────────────────────────────────────────────┐
// │ TIER 1 — EARLY KILL (Protection)                                           │
// │ Purpose: Kill obvious losers FAST                                          │
// │ Trigger immediate retirement if ANY:                                        │
// │   - First 10 trades expectancy < -0.5 bps                                  │
// │   - Win-rate < 30% after 12 trades                                         │
// │   - Net edge after costs negative for 2 consecutive sessions               │
// │   - Slippage > modeled slippage by > 1.2×                                  │
// │ 👉 This saves you DAYS.                                                    │
// ├─────────────────────────────────────────────────────────────────────────────┤
// │ TIER 2 — SOFT PROMOTE (Speed)                                              │
// │ Purpose: Start micro-live SOONER, safely                                   │
// │ Allow micro-live sizing (0.1× min size) when:                              │
// │   - ≥ 20 shadow trades                                                     │
// │   - Expectancy ≥ +0.4 bps                                                  │
// │   - No drawdown > 1.5× planned SL                                          │
// │   - ExecutionQuality score ≥ B                                             │
// │ ⚠️ Still:                                                                   │
// │   - Kill-on-first-loss                                                     │
// │   - No pyramiding                                                          │
// │   - No scale-up                                                            │
// ├─────────────────────────────────────────────────────────────────────────────┤
// │ TIER 3 — FULL PROMOTE (Trust)                                              │
// │ Purpose: Capital trust                                                      │
// │ Enable when:                                                               │
// │   - ≥ 50 trades                                                            │
// │   - Expectancy ≥ +0.6 bps                                                  │
// │   - Stable cost attribution                                                │
// │   - No execution degradation                                               │
// │ Then allow:                                                                │
// │   - Normal sizing                                                          │
// │   - Session weights                                                        │
// │   - Regime-specific SL/TP widening                                         │
// └─────────────────────────────────────────────────────────────────────────────┘
//
// ALPHA-SPECIFIC TUNING:
//   LVC (Liquidity Vacuum): Use 20-trade soft promote (burst-driven, edge shows fast)
//   MTP (Micro Trend Pullback): Use 50-trade full promote (trend confirmation is slower)
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <array>

#include "alpha/MarketRegime.hpp"

namespace Chimera {
namespace Alpha {

// ─────────────────────────────────────────────────────────────────────────────
// Promotion Tier
// ─────────────────────────────────────────────────────────────────────────────
enum class PromotionTier : uint8_t {
    SHADOW = 0,         // No orders, virtual only
    PROBE = 1,          // Micro-live, 0.1× size, kill-on-first-loss
    PARTIAL = 2,        // Partial size, 0.5×
    FULL = 3,           // Full regime cap
    RETIRED = 4         // Killed, no recovery without manual reset
};

inline const char* tierStr(PromotionTier t) {
    switch (t) {
        case PromotionTier::SHADOW:  return "SHADOW";
        case PromotionTier::PROBE:   return "PROBE";
        case PromotionTier::PARTIAL: return "PARTIAL";
        case PromotionTier::FULL:    return "FULL";
        case PromotionTier::RETIRED: return "RETIRED";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Execution Quality Grade
// ─────────────────────────────────────────────────────────────────────────────
enum class ExecGrade : uint8_t {
    A = 0,    // Excellent: cost_error < 0, reject_rate < 2%
    B = 1,    // Good: cost_error < 0.3, reject_rate < 5%
    C = 2,    // Acceptable: cost_error < 0.5, reject_rate < 10%
    D = 3,    // Degraded: cost_error < 0.7
    F = 4     // Failed: cost_error >= 0.7 or reject_rate >= 15%
};

inline const char* gradeStr(ExecGrade g) {
    switch (g) {
        case ExecGrade::A: return "A";
        case ExecGrade::B: return "B";
        case ExecGrade::C: return "C";
        case ExecGrade::D: return "D";
        case ExecGrade::F: return "F";
        default: return "?";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Promotion State (per alpha + symbol)
// ─────────────────────────────────────────────────────────────────────────────
struct PromotionState {
    char alpha_name[32] = {};
    char symbol[16] = {};
    
    // Current tier
    PromotionTier tier = PromotionTier::SHADOW;
    
    // Stats
    uint64_t shadow_trades = 0;
    uint64_t probe_trades = 0;
    uint64_t live_trades = 0;
    
    double shadow_expectancy = 0.0;
    double probe_expectancy = 0.0;
    double live_expectancy = 0.0;
    
    double win_rate = 0.0;
    double max_dd_bps = 0.0;
    double planned_sl_bps = 1.5;        // From alpha params
    
    // Execution quality
    ExecGrade exec_grade = ExecGrade::C;
    double cost_error_bps = 0.0;
    double reject_rate = 0.0;
    double slippage_ratio = 1.0;        // realized / expected
    
    // Session tracking
    uint32_t consecutive_negative_sessions = 0;
    double last_session_edge = 0.0;
    
    // Probe-specific
    bool kill_on_first_loss = true;
    uint32_t probe_losses = 0;
    
    // Timestamps
    uint64_t promoted_to_probe_ts = 0;
    uint64_t promoted_to_live_ts = 0;
    uint64_t last_update_ts = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Promotion Decision
// ─────────────────────────────────────────────────────────────────────────────
struct PromotionDecision {
    PromotionTier new_tier = PromotionTier::SHADOW;
    bool changed = false;
    const char* reason = nullptr;
    
    double size_multiplier = 0.0;       // 0.0 (shadow) to 1.0 (full)
    bool kill_on_first_loss = false;
    bool allow_pyramiding = false;
    bool allow_scale_up = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Alpha-Specific Thresholds
// ─────────────────────────────────────────────────────────────────────────────
struct PromotionThresholds {
    // Tier 1 - Early Kill
    uint32_t tier1_min_trades = 10;
    double tier1_min_expectancy = -0.5;   // Kill if below this
    double tier1_min_win_rate = 0.30;     // Kill if below this after 12 trades
    double tier1_max_slippage_ratio = 1.2;
    uint32_t tier1_max_negative_sessions = 2;
    
    // Tier 2 - Soft Promote
    uint32_t tier2_min_trades = 20;
    double tier2_min_expectancy = 0.4;
    double tier2_max_dd_ratio = 1.5;      // Max DD relative to SL
    ExecGrade tier2_min_exec_grade = ExecGrade::B;
    
    // Tier 3 - Full Promote
    uint32_t tier3_min_trades = 50;
    double tier3_min_expectancy = 0.6;
    double tier3_max_cost_error = 0.3;
    double tier3_max_reject_rate = 0.05;
    
    // Factory for alpha-specific thresholds
    static PromotionThresholds forLVC() {
        PromotionThresholds t;
        // LVC is burst-driven, edge shows quickly
        t.tier2_min_trades = 20;          // Faster soft promote
        t.tier2_min_expectancy = 0.4;
        t.tier3_min_trades = 40;          // Slightly faster full
        return t;
    }
    
    static PromotionThresholds forMTP() {
        PromotionThresholds t;
        // MTP needs more confirmation
        t.tier2_min_trades = 30;          // Slower soft promote
        t.tier2_min_expectancy = 0.5;
        t.tier3_min_trades = 50;          // Standard full
        return t;
    }
    
    static PromotionThresholds forAlpha(const char* alpha_name) {
        if (strstr(alpha_name, "LiquidityVacuum") != nullptr ||
            strstr(alpha_name, "LVC") != nullptr) {
            return forLVC();
        }
        if (strstr(alpha_name, "MicroTrendPullback") != nullptr ||
            strstr(alpha_name, "MTP") != nullptr) {
            return forMTP();
        }
        // Default
        return PromotionThresholds{};
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// TIERED PROMOTION MANAGER
// ─────────────────────────────────────────────────────────────────────────────
class TieredPromotion {
public:
    static constexpr size_t MAX_STATES = 64;
    
    static TieredPromotion& instance() {
        static TieredPromotion tp;
        return tp;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // EVALUATE PROMOTION (call after each trade)
    // ═══════════════════════════════════════════════════════════════════════
    
    PromotionDecision evaluate(
        const char* alpha_name,
        const char* symbol,
        uint64_t n_trades,
        double expectancy_bps,
        double win_rate,
        double max_dd_bps,
        double planned_sl_bps,
        double cost_error_bps,
        double reject_rate,
        double slippage_ratio,
        double last_pnl_bps,
        uint64_t now_ns
    ) {
        PromotionState* state = getOrCreateState(alpha_name, symbol);
        if (!state) return PromotionDecision{};
        
        PromotionThresholds thresh = PromotionThresholds::forAlpha(alpha_name);
        
        // Update state
        state->shadow_trades = n_trades;
        state->shadow_expectancy = expectancy_bps;
        state->win_rate = win_rate;
        state->max_dd_bps = max_dd_bps;
        state->planned_sl_bps = planned_sl_bps;
        state->cost_error_bps = cost_error_bps;
        state->reject_rate = reject_rate;
        state->slippage_ratio = slippage_ratio;
        state->exec_grade = calculateExecGrade(cost_error_bps, reject_rate);
        state->last_update_ts = now_ns;
        
        // Track probe losses
        if (state->tier == PromotionTier::PROBE && last_pnl_bps < 0) {
            ++state->probe_losses;
        }
        
        PromotionDecision decision;
        decision.new_tier = state->tier;
        
        // ─────────────────────────────────────────────────────────────────────
        // TIER 1: Early Kill Checks (any tier)
        // ─────────────────────────────────────────────────────────────────────
        if (checkEarlyKill(state, thresh, decision)) {
            applyDecision(state, decision, now_ns);
            return decision;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Promotion logic based on current tier
        // ─────────────────────────────────────────────────────────────────────
        switch (state->tier) {
            case PromotionTier::SHADOW:
                checkShadowToProbe(state, thresh, decision);
                break;
                
            case PromotionTier::PROBE:
                checkProbeToPartial(state, thresh, decision);
                break;
                
            case PromotionTier::PARTIAL:
                checkPartialToFull(state, thresh, decision);
                break;
                
            case PromotionTier::FULL:
                // Already at full - check for demotion
                checkFullDemotion(state, thresh, decision);
                break;
                
            case PromotionTier::RETIRED:
                // No recovery without manual reset
                decision.reason = "RETIRED";
                break;
        }
        
        applyDecision(state, decision, now_ns);
        return decision;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // SESSION END (call at end of trading session)
    // ═══════════════════════════════════════════════════════════════════════
    
    void onSessionEnd(const char* alpha_name, const char* symbol, double session_edge) {
        PromotionState* state = getState(alpha_name, symbol);
        if (!state) return;
        
        state->last_session_edge = session_edge;
        
        if (session_edge < 0) {
            ++state->consecutive_negative_sessions;
        } else {
            state->consecutive_negative_sessions = 0;
        }
        
        // Check for consecutive negative sessions kill
        if (state->consecutive_negative_sessions >= 2) {
            printf("[PROMOTION] ⚠️  %s %s: 2 consecutive negative sessions\n",
                   alpha_name, symbol);
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // MANUAL CONTROLS
    // ═══════════════════════════════════════════════════════════════════════
    
    void forceRetire(const char* alpha_name, const char* symbol, const char* reason) {
        PromotionState* state = getState(alpha_name, symbol);
        if (state) {
            state->tier = PromotionTier::RETIRED;
            printf("[PROMOTION] ⛔ FORCED RETIREMENT: %s %s - %s\n",
                   alpha_name, symbol, reason);
        }
    }
    
    void resetToShadow(const char* alpha_name, const char* symbol) {
        PromotionState* state = getState(alpha_name, symbol);
        if (state) {
            state->tier = PromotionTier::SHADOW;
            state->shadow_trades = 0;
            state->probe_trades = 0;
            state->probe_losses = 0;
            state->consecutive_negative_sessions = 0;
            printf("[PROMOTION] 🔄 RESET TO SHADOW: %s %s\n", alpha_name, symbol);
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // QUERIES
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] PromotionTier getTier(const char* alpha, const char* symbol) const {
        const PromotionState* state = getState(alpha, symbol);
        return state ? state->tier : PromotionTier::SHADOW;
    }
    
    [[nodiscard]] double getSizeMultiplier(const char* alpha, const char* symbol) const {
        PromotionTier tier = getTier(alpha, symbol);
        switch (tier) {
            case PromotionTier::SHADOW:  return 0.0;
            case PromotionTier::PROBE:   return 0.1;
            case PromotionTier::PARTIAL: return 0.5;
            case PromotionTier::FULL:    return 1.0;
            case PromotionTier::RETIRED: return 0.0;
            default: return 0.0;
        }
    }
    
    [[nodiscard]] bool shouldKillOnFirstLoss(const char* alpha, const char* symbol) const {
        const PromotionState* state = getState(alpha, symbol);
        return state && state->tier == PromotionTier::PROBE;
    }
    
    [[nodiscard]] bool isRetired(const char* alpha, const char* symbol) const {
        const PromotionState* state = getState(alpha, symbol);
        return state && state->tier == PromotionTier::RETIRED;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // REPORTING
    // ═══════════════════════════════════════════════════════════════════════
    
    void printStatus() const {
        printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
        printf("║ TIERED PROMOTION STATUS                                              ║\n");
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        
        for (size_t i = 0; i < state_count_; ++i) {
            const PromotionState& s = states_[i];
            printf("║ %-16s %-8s [%s] trades=%llu exp=%.2f wr=%.0f%% exec=%s\n",
                   s.alpha_name, s.symbol,
                   tierStr(s.tier),
                   static_cast<unsigned long long>(s.shadow_trades),
                   s.shadow_expectancy,
                   s.win_rate * 100.0,
                   gradeStr(s.exec_grade));
            
            if (s.tier == PromotionTier::PROBE) {
                printf("║   └─ PROBE: losses=%u kill_on_first=%s\n",
                       s.probe_losses, s.kill_on_first_loss ? "YES" : "NO");
            }
        }
        
        printf("╚══════════════════════════════════════════════════════════════════════╝\n");
    }

private:
    TieredPromotion() = default;
    
    // ═══════════════════════════════════════════════════════════════════════
    // TIER CHECKS
    // ═══════════════════════════════════════════════════════════════════════
    
    bool checkEarlyKill(PromotionState* s, const PromotionThresholds& t, PromotionDecision& d) {
        // First 10 trades: expectancy < -0.5
        if (s->shadow_trades >= t.tier1_min_trades && s->shadow_expectancy < t.tier1_min_expectancy) {
            d.new_tier = PromotionTier::RETIRED;
            d.changed = true;
            d.reason = "TIER1_EXPECTANCY_KILL";
            d.size_multiplier = 0.0;
            printf("[PROMOTION] ⛔ TIER1 KILL: %s %s exp=%.2f < %.2f after %llu trades\n",
                   s->alpha_name, s->symbol, s->shadow_expectancy, t.tier1_min_expectancy,
                   static_cast<unsigned long long>(s->shadow_trades));
            return true;
        }
        
        // Win rate < 30% after 12 trades
        if (s->shadow_trades >= 12 && s->win_rate < t.tier1_min_win_rate) {
            d.new_tier = PromotionTier::RETIRED;
            d.changed = true;
            d.reason = "TIER1_WINRATE_KILL";
            d.size_multiplier = 0.0;
            printf("[PROMOTION] ⛔ TIER1 KILL: %s %s wr=%.0f%% < %.0f%% after %llu trades\n",
                   s->alpha_name, s->symbol, s->win_rate * 100.0, t.tier1_min_win_rate * 100.0,
                   static_cast<unsigned long long>(s->shadow_trades));
            return true;
        }
        
        // Slippage > 1.2× expected
        if (s->slippage_ratio > t.tier1_max_slippage_ratio && s->shadow_trades >= 10) {
            d.new_tier = PromotionTier::RETIRED;
            d.changed = true;
            d.reason = "TIER1_SLIPPAGE_KILL";
            d.size_multiplier = 0.0;
            printf("[PROMOTION] ⛔ TIER1 KILL: %s %s slippage_ratio=%.2f > %.2f\n",
                   s->alpha_name, s->symbol, s->slippage_ratio, t.tier1_max_slippage_ratio);
            return true;
        }
        
        // 2 consecutive negative sessions
        if (s->consecutive_negative_sessions >= t.tier1_max_negative_sessions) {
            d.new_tier = PromotionTier::RETIRED;
            d.changed = true;
            d.reason = "TIER1_SESSION_KILL";
            d.size_multiplier = 0.0;
            printf("[PROMOTION] ⛔ TIER1 KILL: %s %s %u consecutive negative sessions\n",
                   s->alpha_name, s->symbol, s->consecutive_negative_sessions);
            return true;
        }
        
        // Probe kill-on-first-loss
        if (s->tier == PromotionTier::PROBE && s->kill_on_first_loss && s->probe_losses > 0) {
            d.new_tier = PromotionTier::SHADOW;  // Demote back, not retire
            d.changed = true;
            d.reason = "PROBE_FIRST_LOSS";
            d.size_multiplier = 0.0;
            printf("[PROMOTION] ⚠️  PROBE DEMOTE: %s %s first loss in probe mode\n",
                   s->alpha_name, s->symbol);
            return true;
        }
        
        return false;
    }
    
    void checkShadowToProbe(PromotionState* s, const PromotionThresholds& t, PromotionDecision& d) {
        // Need: ≥ 20 trades, exp ≥ +0.4, DD < 1.5× SL, exec grade ≥ B
        if (s->shadow_trades < t.tier2_min_trades) {
            d.reason = "NEED_MORE_SHADOW_TRADES";
            d.size_multiplier = 0.0;
            return;
        }
        
        if (s->shadow_expectancy < t.tier2_min_expectancy) {
            d.reason = "EXPECTANCY_TOO_LOW";
            d.size_multiplier = 0.0;
            return;
        }
        
        double dd_ratio = s->max_dd_bps / s->planned_sl_bps;
        if (dd_ratio > t.tier2_max_dd_ratio) {
            d.reason = "DRAWDOWN_TOO_HIGH";
            d.size_multiplier = 0.0;
            return;
        }
        
        if (s->exec_grade > t.tier2_min_exec_grade) {
            d.reason = "EXEC_QUALITY_TOO_LOW";
            d.size_multiplier = 0.0;
            return;
        }
        
        // PROMOTE TO PROBE
        d.new_tier = PromotionTier::PROBE;
        d.changed = true;
        d.reason = "PROMOTED_TO_PROBE";
        d.size_multiplier = 0.1;
        d.kill_on_first_loss = true;
        d.allow_pyramiding = false;
        d.allow_scale_up = false;
        
        printf("[PROMOTION] ✓ TIER2 PROMOTE: %s %s → PROBE (0.1× size, kill-on-first-loss)\n",
               s->alpha_name, s->symbol);
    }
    
    void checkProbeToPartial(PromotionState* s, const PromotionThresholds& t, PromotionDecision& d) {
        // Need: ≥ 35 trades, exp ≥ +0.5, survived probe without loss
        if (s->shadow_trades < 35) {
            d.reason = "NEED_MORE_PROBE_TRADES";
            d.size_multiplier = 0.1;
            d.kill_on_first_loss = true;
            return;
        }
        
        if (s->shadow_expectancy < 0.5) {
            d.reason = "EXPECTANCY_NOT_STRONG_ENOUGH";
            d.size_multiplier = 0.1;
            d.kill_on_first_loss = true;
            return;
        }
        
        // PROMOTE TO PARTIAL
        d.new_tier = PromotionTier::PARTIAL;
        d.changed = true;
        d.reason = "PROMOTED_TO_PARTIAL";
        d.size_multiplier = 0.5;
        d.kill_on_first_loss = false;
        d.allow_pyramiding = false;
        d.allow_scale_up = false;
        
        printf("[PROMOTION] ✓ TIER2→3 PROMOTE: %s %s → PARTIAL (0.5× size)\n",
               s->alpha_name, s->symbol);
    }
    
    void checkPartialToFull(PromotionState* s, const PromotionThresholds& t, PromotionDecision& d) {
        // Need: ≥ 50 trades, exp ≥ +0.6, cost_error < 0.3, reject < 5%
        if (s->shadow_trades < t.tier3_min_trades) {
            d.reason = "NEED_MORE_TRADES_FOR_FULL";
            d.size_multiplier = 0.5;
            return;
        }
        
        if (s->shadow_expectancy < t.tier3_min_expectancy) {
            d.reason = "EXPECTANCY_NOT_STABLE";
            d.size_multiplier = 0.5;
            return;
        }
        
        if (s->cost_error_bps > t.tier3_max_cost_error) {
            d.reason = "COST_ERROR_TOO_HIGH";
            d.size_multiplier = 0.5;
            return;
        }
        
        if (s->reject_rate > t.tier3_max_reject_rate) {
            d.reason = "REJECT_RATE_TOO_HIGH";
            d.size_multiplier = 0.5;
            return;
        }
        
        // PROMOTE TO FULL
        d.new_tier = PromotionTier::FULL;
        d.changed = true;
        d.reason = "PROMOTED_TO_FULL";
        d.size_multiplier = 1.0;
        d.kill_on_first_loss = false;
        d.allow_pyramiding = false;  // Still no pyramiding
        d.allow_scale_up = true;     // Now can adjust within regime caps
        
        printf("[PROMOTION] ✓ TIER3 PROMOTE: %s %s → FULL (1.0× size, session weights enabled)\n",
               s->alpha_name, s->symbol);
    }
    
    void checkFullDemotion(PromotionState* s, const PromotionThresholds& t, PromotionDecision& d) {
        // Demote if exec quality degrades significantly
        if (s->exec_grade >= ExecGrade::D) {
            d.new_tier = PromotionTier::PARTIAL;
            d.changed = true;
            d.reason = "EXEC_QUALITY_DEGRADED";
            d.size_multiplier = 0.5;
            printf("[PROMOTION] ⚠️  DEMOTE: %s %s FULL → PARTIAL (exec grade %s)\n",
                   s->alpha_name, s->symbol, gradeStr(s->exec_grade));
            return;
        }
        
        // Demote if expectancy drops significantly
        if (s->shadow_expectancy < 0.3 && s->shadow_trades > 70) {
            d.new_tier = PromotionTier::PARTIAL;
            d.changed = true;
            d.reason = "EXPECTANCY_DECAY";
            d.size_multiplier = 0.5;
            printf("[PROMOTION] ⚠️  DEMOTE: %s %s FULL → PARTIAL (exp=%.2f)\n",
                   s->alpha_name, s->symbol, s->shadow_expectancy);
            return;
        }
        
        d.size_multiplier = 1.0;
        d.reason = "FULL_STABLE";
    }
    
    void applyDecision(PromotionState* s, const PromotionDecision& d, uint64_t now_ns) {
        if (d.changed) {
            PromotionTier old_tier = s->tier;
            s->tier = d.new_tier;
            
            if (d.new_tier == PromotionTier::PROBE && old_tier == PromotionTier::SHADOW) {
                s->promoted_to_probe_ts = now_ns;
                s->probe_trades = 0;
                s->probe_losses = 0;
                s->kill_on_first_loss = d.kill_on_first_loss;
            }
            
            if (d.new_tier == PromotionTier::FULL) {
                s->promoted_to_live_ts = now_ns;
            }
        }
    }
    
    ExecGrade calculateExecGrade(double cost_error, double reject_rate) const {
        if (cost_error < 0 && reject_rate < 0.02) return ExecGrade::A;
        if (cost_error < 0.3 && reject_rate < 0.05) return ExecGrade::B;
        if (cost_error < 0.5 && reject_rate < 0.10) return ExecGrade::C;
        if (cost_error < 0.7) return ExecGrade::D;
        return ExecGrade::F;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // STATE MANAGEMENT
    // ═══════════════════════════════════════════════════════════════════════
    
    PromotionState* getOrCreateState(const char* alpha, const char* symbol) {
        for (size_t i = 0; i < state_count_; ++i) {
            if (strcmp(states_[i].alpha_name, alpha) == 0 &&
                strcmp(states_[i].symbol, symbol) == 0) {
                return &states_[i];
            }
        }
        
        if (state_count_ >= MAX_STATES) return nullptr;
        
        PromotionState& s = states_[state_count_++];
        strncpy(s.alpha_name, alpha, 31);
        strncpy(s.symbol, symbol, 15);
        return &s;
    }
    
    PromotionState* getState(const char* alpha, const char* symbol) {
        for (size_t i = 0; i < state_count_; ++i) {
            if (strcmp(states_[i].alpha_name, alpha) == 0 &&
                strcmp(states_[i].symbol, symbol) == 0) {
                return &states_[i];
            }
        }
        return nullptr;
    }
    
    const PromotionState* getState(const char* alpha, const char* symbol) const {
        for (size_t i = 0; i < state_count_; ++i) {
            if (strcmp(states_[i].alpha_name, alpha) == 0 &&
                strcmp(states_[i].symbol, symbol) == 0) {
                return &states_[i];
            }
        }
        return nullptr;
    }
    
    std::array<PromotionState, MAX_STATES> states_{};
    size_t state_count_ = 0;
};

} // namespace Alpha
} // namespace Chimera
