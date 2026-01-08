// ═══════════════════════════════════════════════════════════════════════════════
// include/alpha/MissedOpportunityAuditor.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.24: MISSED OPPORTUNITY AUDITOR (MOA)
// STATUS: 🔧 ACTIVE
// OWNER: Jo
// CREATED: 2026-01-03
//
// THE PROBLEM:
//   You don't know: "What trades we should have taken but didn't"
//
//   This blinds you to:
//   - Over-filtering
//   - Excessive gating
//   - Latency paranoia
//
// THE SOLUTION:
//   Shadow-only, zero risk monitoring of blocked trades.
//
//   Whenever:
//   - Alpha signal passes structure
//   - BUT trade is blocked by a gate
//   
//   Log the opportunity and track what would have happened.
//
// WHAT WE CAPTURE:
//   - Alpha, symbol
//   - Predicted edge
//   - Gate reason (why blocked)
//   - Future MFE/MAE (what actually happened)
//   - Hypothetical outcome (TP/SL simulation)
//
// OUTPUT INSIGHTS:
//   "ExecutionGovernor blocked 23 trades last week
//    14 would have been winners
//    Mean missed edge: +1.2 bps"
//
//   This tells you WHERE TO LOOSEN SAFELY.
//
// HOW IT CONNECTS:
//   Trade → Realized Cost → FeedbackController
//   Feedback → Alpha Trust → Confidence Score
//   Confidence → Position Size
//   Loss Shapes → Early Exit / Throttle
//   Missed Trades → Gate Calibration  ← THIS MODULE
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <array>
#include <fstream>

namespace Chimera {
namespace Alpha {

// ─────────────────────────────────────────────────────────────────────────────
// Gate Reason (why was trade blocked?)
// ─────────────────────────────────────────────────────────────────────────────
enum class GateReason : uint8_t {
    NONE = 0,                  // Not blocked
    
    // Execution gates
    EXEC_GOVERNOR_DEGRADED,    // Venue in degraded state
    EXEC_GOVERNOR_HALTED,      // Venue halted
    LATENCY_TOO_HIGH,          // Latency p95 exceeded threshold
    REJECT_RATE_HIGH,          // Recent reject rate too high
    COST_ERROR_HIGH,           // Cost error too high
    
    // Alpha gates
    REGIME_BLOCKED,            // Wrong regime
    SPREAD_TOO_WIDE,           // Spread exceeded threshold
    EXPECTANCY_LOW,            // Alpha expectancy below threshold
    CONFIDENCE_LOW,            // Confidence score too low
    THROTTLED,                 // Frequency throttled
    
    // Risk gates
    DAILY_LOSS_LIMIT,          // Daily loss limit hit
    POSITION_LIMIT,            // Max position reached
    EXPOSURE_LIMIT,            // Max exposure reached
    SYMBOL_DISABLED,           // Symbol disabled by risk
    KILL_SWITCH,               // Global kill switch
    
    // Promotion gates
    SHADOW_ONLY,               // Still in shadow mode
    PROBE_FIRST_LOSS,          // Probe mode, had first loss
    
    // Session gates
    SESSION_CLOSED,            // Outside trading hours
    NEWS_BLACKOUT,             // News gate active
    
    COUNT
};

inline const char* gateReasonStr(GateReason r) {
    switch (r) {
        case GateReason::NONE:                    return "NONE";
        case GateReason::EXEC_GOVERNOR_DEGRADED:  return "EXEC_DEGRADED";
        case GateReason::EXEC_GOVERNOR_HALTED:    return "EXEC_HALTED";
        case GateReason::LATENCY_TOO_HIGH:        return "LATENCY_HIGH";
        case GateReason::REJECT_RATE_HIGH:        return "REJECT_RATE";
        case GateReason::COST_ERROR_HIGH:         return "COST_ERROR";
        case GateReason::REGIME_BLOCKED:          return "REGIME_BLOCKED";
        case GateReason::SPREAD_TOO_WIDE:         return "SPREAD_WIDE";
        case GateReason::EXPECTANCY_LOW:          return "EXP_LOW";
        case GateReason::CONFIDENCE_LOW:          return "CONF_LOW";
        case GateReason::THROTTLED:               return "THROTTLED";
        case GateReason::DAILY_LOSS_LIMIT:        return "DAILY_LOSS";
        case GateReason::POSITION_LIMIT:          return "POS_LIMIT";
        case GateReason::EXPOSURE_LIMIT:          return "EXP_LIMIT";
        case GateReason::SYMBOL_DISABLED:         return "SYM_DISABLED";
        case GateReason::KILL_SWITCH:             return "KILL_SWITCH";
        case GateReason::SHADOW_ONLY:             return "SHADOW_ONLY";
        case GateReason::PROBE_FIRST_LOSS:        return "PROBE_LOSS";
        case GateReason::SESSION_CLOSED:          return "SESSION_CLOSED";
        case GateReason::NEWS_BLACKOUT:           return "NEWS_BLACKOUT";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Missed Opportunity Record
// ─────────────────────────────────────────────────────────────────────────────
struct MissedOpportunity {
    // Identification
    uint64_t opp_id = 0;
    char alpha_name[32] = {};
    char symbol[16] = {};
    uint64_t timestamp_ns = 0;
    
    // Signal at time of block
    double predicted_edge_bps = 0.0;
    double spread_bps = 0.0;
    double ofi = 0.0;
    double mid_price = 0.0;
    bool was_buy = true;
    
    // Why blocked
    GateReason blocked_by = GateReason::NONE;
    double gate_value = 0.0;           // The value that triggered the gate
    double gate_threshold = 0.0;       // The threshold it exceeded
    
    // What actually happened (filled in later)
    double future_mfe_bps = 0.0;       // Max favorable excursion
    double future_mae_bps = 0.0;       // Max adverse excursion
    double future_close_bps = 0.0;     // PnL at planned exit time
    bool resolved = false;
    uint64_t resolved_ts_ns = 0;
    
    // Hypothetical outcome
    double hypothetical_pnl_bps = 0.0;
    bool would_have_won = false;
    
    // Planned exit levels (for simulation)
    double tp_bps = 2.0;
    double sl_bps = 1.5;
    uint64_t max_hold_ns = 400'000'000ULL;
};

// ─────────────────────────────────────────────────────────────────────────────
// Gate Statistics
// ─────────────────────────────────────────────────────────────────────────────
struct GateStats {
    GateReason reason = GateReason::NONE;
    
    uint64_t total_blocks = 0;
    uint64_t would_have_won = 0;
    uint64_t would_have_lost = 0;
    
    double total_missed_edge = 0.0;    // Sum of hypothetical PnL
    double avg_missed_edge = 0.0;
    
    double avg_gate_value = 0.0;       // Average value that triggered gate
    double avg_gate_margin = 0.0;      // Avg distance from threshold
    
    // Best loosening candidates
    double loosest_safe_threshold = 0.0;
    
    void update(const MissedOpportunity& opp) {
        ++total_blocks;
        
        if (opp.would_have_won) {
            ++would_have_won;
            total_missed_edge += opp.hypothetical_pnl_bps;
        } else {
            ++would_have_lost;
        }
        
        avg_missed_edge = (total_blocks > 0) ? total_missed_edge / total_blocks : 0.0;
        avg_gate_value = (avg_gate_value * (total_blocks - 1) + opp.gate_value) / total_blocks;
        avg_gate_margin = (avg_gate_margin * (total_blocks - 1) + 
                          (opp.gate_value - opp.gate_threshold)) / total_blocks;
    }
    
    [[nodiscard]] double win_rate() const noexcept {
        return (total_blocks > 0) ? static_cast<double>(would_have_won) / total_blocks : 0.0;
    }
    
    [[nodiscard]] bool worth_loosening() const noexcept {
        // Worth loosening if >60% would have been winners AND positive edge
        return total_blocks >= 10 && win_rate() > 0.60 && avg_missed_edge > 0.3;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// MISSED OPPORTUNITY AUDITOR
// ─────────────────────────────────────────────────────────────────────────────
class MissedOpportunityAuditor {
public:
    static constexpr size_t MAX_PENDING = 256;       // Max unresolved opportunities
    static constexpr size_t GATE_COUNT = static_cast<size_t>(GateReason::COUNT);
    
    static MissedOpportunityAuditor& instance() {
        static MissedOpportunityAuditor moa;
        return moa;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // RECORD MISSED OPPORTUNITY
    // ═══════════════════════════════════════════════════════════════════════
    
    void recordBlocked(
        const char* alpha,
        const char* symbol,
        GateReason reason,
        double gate_value,
        double gate_threshold,
        double predicted_edge_bps,
        double spread_bps,
        double ofi,
        double mid_price,
        bool is_buy,
        double tp_bps,
        double sl_bps,
        uint64_t max_hold_ns,
        uint64_t now_ns
    ) {
        // Find free slot
        MissedOpportunity* slot = nullptr;
        for (size_t i = 0; i < MAX_PENDING; ++i) {
            if (pending_[i].opp_id == 0) {
                slot = &pending_[i];
                break;
            }
        }
        
        if (!slot) {
            // Evict oldest
            slot = &pending_[0];
            for (size_t i = 1; i < MAX_PENDING; ++i) {
                if (pending_[i].timestamp_ns < slot->timestamp_ns) {
                    slot = &pending_[i];
                }
            }
            
            // Resolve evicted if not done
            if (!slot->resolved) {
                resolveAsTimeout(*slot);
            }
        }
        
        // Initialize
        slot->opp_id = ++opp_counter_;
        strncpy(slot->alpha_name, alpha, 31);
        strncpy(slot->symbol, symbol, 15);
        slot->timestamp_ns = now_ns;
        slot->predicted_edge_bps = predicted_edge_bps;
        slot->spread_bps = spread_bps;
        slot->ofi = ofi;
        slot->mid_price = mid_price;
        slot->was_buy = is_buy;
        slot->blocked_by = reason;
        slot->gate_value = gate_value;
        slot->gate_threshold = gate_threshold;
        slot->tp_bps = tp_bps;
        slot->sl_bps = sl_bps;
        slot->max_hold_ns = max_hold_ns;
        slot->future_mfe_bps = 0.0;
        slot->future_mae_bps = 0.0;
        slot->future_close_bps = 0.0;
        slot->resolved = false;
        
        printf("[MOA] Blocked: %s %s by %s (val=%.2f thresh=%.2f edge=%.2f)\n",
               alpha, symbol, gateReasonStr(reason),
               gate_value, gate_threshold, predicted_edge_bps);
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // UPDATE WITH MARKET DATA (call on ticks)
    // ═══════════════════════════════════════════════════════════════════════
    
    void updateMarket(const char* symbol, double mid_price, uint64_t now_ns) {
        for (size_t i = 0; i < MAX_PENDING; ++i) {
            MissedOpportunity& opp = pending_[i];
            
            if (opp.opp_id == 0 || opp.resolved) continue;
            if (strcmp(opp.symbol, symbol) != 0) continue;
            
            // Calculate current PnL
            double pnl = opp.was_buy
                ? (mid_price - opp.mid_price) / opp.mid_price * 10000.0
                : (opp.mid_price - mid_price) / opp.mid_price * 10000.0;
            
            // Update MFE/MAE
            if (pnl > opp.future_mfe_bps) opp.future_mfe_bps = pnl;
            if (pnl < opp.future_mae_bps) opp.future_mae_bps = pnl;
            
            // Check for resolution
            bool hit_tp = pnl >= opp.tp_bps;
            bool hit_sl = pnl <= -opp.sl_bps;
            bool timeout = (now_ns - opp.timestamp_ns) >= opp.max_hold_ns;
            
            if (hit_tp) {
                resolve(opp, pnl, true, now_ns);
            } else if (hit_sl) {
                resolve(opp, pnl, false, now_ns);
            } else if (timeout) {
                resolve(opp, pnl, pnl > 0, now_ns);
            }
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // QUERIES
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] const GateStats& getGateStats(GateReason reason) const {
        return gate_stats_[static_cast<size_t>(reason)];
    }
    
    [[nodiscard]] bool shouldLoosenGate(GateReason reason) const {
        return gate_stats_[static_cast<size_t>(reason)].worth_loosening();
    }
    
    [[nodiscard]] double suggestedThreshold(GateReason reason) const {
        const GateStats& stats = gate_stats_[static_cast<size_t>(reason)];
        if (!stats.worth_loosening()) return 0.0;
        
        // Suggest loosening by the average margin that blocked winners
        // This is a heuristic - needs human review before applying
        return stats.avg_gate_margin * 0.5;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // REPORTING
    // ═══════════════════════════════════════════════════════════════════════
    
    void printReport() const {
        printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
        printf("║ MISSED OPPORTUNITY AUDIT REPORT                                      ║\n");
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        
        uint64_t total_blocked = 0;
        uint64_t total_would_won = 0;
        double total_missed_edge = 0.0;
        
        for (size_t i = 0; i < GATE_COUNT; ++i) {
            const GateStats& s = gate_stats_[i];
            if (s.total_blocks == 0) continue;
            
            total_blocked += s.total_blocks;
            total_would_won += s.would_have_won;
            total_missed_edge += s.total_missed_edge;
            
            printf("║ %-18s blocks=%3llu W=%3llu L=%3llu wr=%.0f%% edge=%+.2f %s\n",
                   gateReasonStr(static_cast<GateReason>(i)),
                   static_cast<unsigned long long>(s.total_blocks),
                   static_cast<unsigned long long>(s.would_have_won),
                   static_cast<unsigned long long>(s.would_have_lost),
                   s.win_rate() * 100.0,
                   s.avg_missed_edge,
                   s.worth_loosening() ? "← LOOSEN?" : "");
        }
        
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        printf("║ TOTAL: %llu blocked, %llu would have won (%.0f%%), missed edge: %.1f bps\n",
               static_cast<unsigned long long>(total_blocked),
               static_cast<unsigned long long>(total_would_won),
               total_blocked > 0 ? static_cast<double>(total_would_won) / total_blocked * 100.0 : 0.0,
               total_missed_edge);
        printf("╚══════════════════════════════════════════════════════════════════════╝\n");
    }
    
    void printLooseningSuggestions() const {
        printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
        printf("║ GATE LOOSENING SUGGESTIONS (Review before applying!)                 ║\n");
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        
        bool any_suggestions = false;
        
        for (size_t i = 0; i < GATE_COUNT; ++i) {
            const GateStats& s = gate_stats_[i];
            if (!s.worth_loosening()) continue;
            
            any_suggestions = true;
            GateReason reason = static_cast<GateReason>(i);
            
            printf("║ %s:\n", gateReasonStr(reason));
            printf("║   Blocked %llu trades, %llu (%.0f%%) would have won\n",
                   static_cast<unsigned long long>(s.total_blocks),
                   static_cast<unsigned long long>(s.would_have_won),
                   s.win_rate() * 100.0);
            printf("║   Avg missed edge: +%.2f bps\n", s.avg_missed_edge);
            printf("║   Suggestion: Loosen threshold by ~%.2f\n", suggestedThreshold(reason));
            printf("║\n");
        }
        
        if (!any_suggestions) {
            printf("║ No gates currently warrant loosening.                                ║\n");
        }
        
        printf("╚══════════════════════════════════════════════════════════════════════╝\n");
    }
    
    void writeCSV(const char* filepath) const {
        std::ofstream f(filepath);
        if (!f.is_open()) {
            printf("[MOA] ERROR: Cannot open %s\n", filepath);
            return;
        }
        
        f << "gate,blocks,would_win,would_lose,win_rate,avg_edge,worth_loosen\n";
        
        for (size_t i = 0; i < GATE_COUNT; ++i) {
            const GateStats& s = gate_stats_[i];
            if (s.total_blocks == 0) continue;
            
            f << gateReasonStr(static_cast<GateReason>(i)) << ","
              << s.total_blocks << ","
              << s.would_have_won << ","
              << s.would_have_lost << ","
              << s.win_rate() << ","
              << s.avg_missed_edge << ","
              << (s.worth_loosening() ? "1" : "0") << "\n";
        }
        
        f.close();
        printf("[MOA] Report written to %s\n", filepath);
    }

private:
    MissedOpportunityAuditor() = default;
    
    void resolve(MissedOpportunity& opp, double final_pnl, bool won, uint64_t now_ns) {
        opp.future_close_bps = final_pnl;
        opp.hypothetical_pnl_bps = final_pnl;
        opp.would_have_won = won;
        opp.resolved = true;
        opp.resolved_ts_ns = now_ns;
        
        // Update gate stats
        gate_stats_[static_cast<size_t>(opp.blocked_by)].update(opp);
        
        printf("[MOA] Resolved: %s %s blocked by %s → %s (pnl=%.2f mfe=%.2f mae=%.2f)\n",
               opp.alpha_name, opp.symbol,
               gateReasonStr(opp.blocked_by),
               won ? "WOULD_WIN" : "WOULD_LOSE",
               final_pnl, opp.future_mfe_bps, opp.future_mae_bps);
        
        // Clear slot
        opp.opp_id = 0;
    }
    
    void resolveAsTimeout(MissedOpportunity& opp) {
        // Assume loss for unresolved (conservative)
        opp.hypothetical_pnl_bps = -0.5;
        opp.would_have_won = false;
        opp.resolved = true;
        
        gate_stats_[static_cast<size_t>(opp.blocked_by)].update(opp);
        opp.opp_id = 0;
    }
    
    std::array<MissedOpportunity, MAX_PENDING> pending_{};
    std::array<GateStats, GATE_COUNT> gate_stats_{};
    uint64_t opp_counter_ = 0;
};

} // namespace Alpha
} // namespace Chimera
