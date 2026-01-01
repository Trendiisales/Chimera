#pragma once
// =============================================================================
// KillSwitchLadder.hpp v4.2.2 - Hard Safety System
// =============================================================================
// Non-negotiable safety system that:
//   - Degrades risk in steps
//   - Freezes symbols
//   - Halts engine if necessary
//
// Triggers on: Latency, Slippage, Error rate, Session drawdown
// =============================================================================

#include <cstdint>
#include <string>
#include <iostream>
#include <iomanip>

namespace Omega {

// =============================================================================
// KILL-SWITCH LEVELS (6 tiers - NOT binary)
// =============================================================================
enum class KillSwitchLevel : uint8_t {
    NORMAL = 0,        // Level 0: Full operation
    WARNING = 1,       // Level 1: Log only, no action
    THROTTLE = 2,      // Level 2: Size throttle (50%)
    NO_NEW_ENTRY = 3,  // Level 3: New entries disabled
    SYMBOL_FREEZE = 4, // Level 4: Flatten symbol
    VENUE_HALT = 5,    // Level 5: Venue halt
    GLOBAL_HALT = 6    // Level 6: Global kill
};

inline const char* KillSwitchLevelStr(KillSwitchLevel level) {
    switch (level) {
        case KillSwitchLevel::NORMAL: return "NORMAL";
        case KillSwitchLevel::WARNING: return "WARNING";
        case KillSwitchLevel::THROTTLE: return "THROTTLE";
        case KillSwitchLevel::NO_NEW_ENTRY: return "NO_NEW_ENTRY";
        case KillSwitchLevel::SYMBOL_FREEZE: return "SYMBOL_FREEZE";
        case KillSwitchLevel::VENUE_HALT: return "VENUE_HALT";
        case KillSwitchLevel::GLOBAL_HALT: return "GLOBAL_HALT";
    }
    return "UNKNOWN";
}

// =============================================================================
// RECOVERY STATE
// =============================================================================
enum class RecoveryState : uint8_t {
    RUNNING = 0,
    HALTED = 1,
    COOLING = 2,
    REARMED = 3
};

inline const char* RecoveryStateStr(RecoveryState state) {
    switch (state) {
        case RecoveryState::RUNNING: return "RUNNING";
        case RecoveryState::HALTED: return "HALTED";
        case RecoveryState::COOLING: return "COOLING";
        case RecoveryState::REARMED: return "REARMED";
    }
    return "UNKNOWN";
}

// =============================================================================
// LATENCY STATS - Per-symbol latency tracking
// =============================================================================
struct LatencyStats {
    double ema_rtt_ms = 5.0;      // EMA of round-trip time
    double ema_slippage = 0.0;    // EMA of slippage in bps
    double max_rtt_ms = 0.0;      // Maximum RTT observed
    uint64_t sample_count = 0;
    
    static constexpr double LATENCY_ALPHA = 0.15;
    
    void update(double rtt_ms, double slippage_bps) {
        if (sample_count == 0) {
            ema_rtt_ms = rtt_ms;
            ema_slippage = slippage_bps;
        } else {
            ema_rtt_ms = LATENCY_ALPHA * rtt_ms + (1.0 - LATENCY_ALPHA) * ema_rtt_ms;
            ema_slippage = LATENCY_ALPHA * slippage_bps + (1.0 - LATENCY_ALPHA) * ema_slippage;
        }
        if (rtt_ms > max_rtt_ms) max_rtt_ms = rtt_ms;
        sample_count++;
    }
    
    void reset() {
        ema_rtt_ms = 5.0;
        ema_slippage = 0.0;
        max_rtt_ms = 0.0;
        sample_count = 0;
    }
};

// =============================================================================
// KILL-SWITCH STATS - Aggregated per-symbol safety metrics
// =============================================================================
struct KillSwitchStats {
    double ema_latency_ms = 5.0;
    double ema_slippage = 0.0;
    double session_pnl = 0.0;
    uint32_t error_count = 0;
    uint32_t consecutive_losses = 0;
    uint64_t last_trade_ns = 0;
    
    void recordError() { error_count++; }
    void recordLoss() { consecutive_losses++; }
    void recordWin() { consecutive_losses = 0; }
    void addPnL(double pnl) { session_pnl += pnl; }
    
    void updateLatency(const LatencyStats& lat) {
        ema_latency_ms = lat.ema_rtt_ms;
        ema_slippage = lat.ema_slippage;
    }
    
    void reset() {
        ema_latency_ms = 5.0;
        ema_slippage = 0.0;
        session_pnl = 0.0;
        error_count = 0;
        consecutive_losses = 0;
        last_trade_ns = 0;
    }
};

// =============================================================================
// KILL-SWITCH DECISION
// =============================================================================
struct KillSwitchDecision {
    KillSwitchLevel level = KillSwitchLevel::NORMAL;
    double risk_multiplier = 1.0;
    const char* reason = "";
};

// =============================================================================
// RECOVERY STATS - For resuming after halt
// =============================================================================
struct RecoveryStats {
    double ema_latency_ms = 5.0;
    double ema_slippage = 0.0;
    uint32_t stable_ticks = 0;
    
    void tick(double latency_ms, double slippage) {
        constexpr double alpha = 0.1;
        ema_latency_ms = alpha * latency_ms + (1.0 - alpha) * ema_latency_ms;
        ema_slippage = alpha * slippage + (1.0 - alpha) * ema_slippage;
        
        // Count stable ticks (low latency, low slippage)
        if (latency_ms < 10.0 && slippage < 0.4) {
            stable_ticks++;
        } else {
            stable_ticks = 0;
        }
    }
    
    void reset() {
        ema_latency_ms = 5.0;
        ema_slippage = 0.0;
        stable_ticks = 0;
    }
};

// =============================================================================
// RECOVERY DECISION
// =============================================================================
struct RecoveryDecision {
    bool allow_trading = true;
    RecoveryState state = RecoveryState::RUNNING;
};

// =============================================================================
// THRESHOLDS (Authoritative)
// =============================================================================
namespace KillSwitchThresholds {
    // Latency thresholds (ms)
    constexpr double LATENCY_WARN_MS = 15.0;
    constexpr double LATENCY_HARD_MS = 30.0;
    
    // Slippage thresholds (bps)
    constexpr double SLIPPAGE_WARN = 0.6;
    constexpr double SLIPPAGE_HARD = 1.2;
    
    // Session drawdown thresholds (bps)
    constexpr double SESSION_DD_WARN = -20.0;
    constexpr double SESSION_DD_HARD = -50.0;
    
    // Error counts
    constexpr uint32_t ERROR_WARN_COUNT = 3;
    constexpr uint32_t ERROR_HARD_COUNT = 6;
    
    // Consecutive losses
    constexpr uint32_t CONSEC_LOSS_WARN = 4;
    constexpr uint32_t CONSEC_LOSS_HARD = 7;
    
    // Recovery requirements (STRICT - no auto-resume)
    constexpr double LATENCY_RECOVER_MS = 10.0;
    constexpr double SLIPPAGE_RECOVER = 0.4;
    constexpr uint32_t REQUIRED_STABLE_TICKS = 500;
    constexpr uint64_t MIN_COOLDOWN_NS = 30'000'000'000ULL;  // 30s minimum halt
    constexpr double SPREAD_NORMALIZE_MULT = 1.5;  // Spread must be < 1.5× normal
}

// =============================================================================
// RE-ARM REQUIREMENTS - All must be satisfied to resume
// =============================================================================
struct RearmRequirements {
    bool positions_flat = false;        // No open positions
    bool venue_stable = false;          // Venue health OK for N seconds
    bool latency_normal = false;        // Latency under threshold
    bool spread_normal = false;         // Spread normalized
    bool cooldown_elapsed = false;      // Minimum wait time passed
    bool manual_override = false;       // Manual re-arm (optional)
    
    bool allSatisfied() const {
        return positions_flat && venue_stable && latency_normal && 
               spread_normal && cooldown_elapsed;
    }
    
    void log(const char* symbol) const {
        std::cout << "[RE-ARM " << symbol << "] Requirements: "
                  << "flat=" << positions_flat
                  << " venue=" << venue_stable
                  << " latency=" << latency_normal
                  << " spread=" << spread_normal
                  << " cooldown=" << cooldown_elapsed
                  << " manual=" << manual_override
                  << " → " << (allSatisfied() ? "READY" : "BLOCKED") << "\n";
    }
};

// =============================================================================
// EVALUATE KILL-SWITCH - 6-tier ladder (NOT binary)
// =============================================================================
inline KillSwitchDecision EvaluateKillSwitch(
    [[maybe_unused]] const char* symbol,
    const KillSwitchStats& st
) {
    using namespace KillSwitchThresholds;
    
    // ═══════════════════════════════════════════════════════════════════════
    // LEVEL 6: GLOBAL HALT - Immediate full shutdown
    // ═══════════════════════════════════════════════════════════════════════
    if (st.ema_latency_ms > LATENCY_HARD_MS * 1.5) {
        return { KillSwitchLevel::GLOBAL_HALT, 0.0, "LATENCY_CRITICAL" };
    }
    if (st.ema_slippage > SLIPPAGE_HARD * 1.5) {
        return { KillSwitchLevel::GLOBAL_HALT, 0.0, "SLIPPAGE_CRITICAL" };
    }
    if (st.session_pnl < SESSION_DD_HARD * 1.5) {
        return { KillSwitchLevel::GLOBAL_HALT, 0.0, "DRAWDOWN_CRITICAL" };
    }
    if (st.error_count > ERROR_HARD_COUNT * 2) {
        return { KillSwitchLevel::GLOBAL_HALT, 0.0, "ERROR_CRITICAL" };
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // LEVEL 5: VENUE HALT - Disable entire venue
    // ═══════════════════════════════════════════════════════════════════════
    if (st.ema_latency_ms > LATENCY_HARD_MS) {
        return { KillSwitchLevel::VENUE_HALT, 0.0, "LATENCY_VENUE_HALT" };
    }
    if (st.ema_slippage > SLIPPAGE_HARD) {
        return { KillSwitchLevel::VENUE_HALT, 0.0, "SLIPPAGE_VENUE_HALT" };
    }
    if (st.session_pnl < SESSION_DD_HARD) {
        return { KillSwitchLevel::VENUE_HALT, 0.0, "DRAWDOWN_VENUE_HALT" };
    }
    if (st.error_count > ERROR_HARD_COUNT) {
        return { KillSwitchLevel::VENUE_HALT, 0.0, "ERROR_VENUE_HALT" };
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // LEVEL 4: SYMBOL FREEZE - Flatten and disable this symbol only
    // ═══════════════════════════════════════════════════════════════════════
    if (st.ema_latency_ms > LATENCY_WARN_MS) {
        return { KillSwitchLevel::SYMBOL_FREEZE, 0.0, "LATENCY_FREEZE" };
    }
    if (st.ema_slippage > SLIPPAGE_WARN) {
        return { KillSwitchLevel::SYMBOL_FREEZE, 0.0, "SLIPPAGE_FREEZE" };
    }
    if (st.session_pnl < SESSION_DD_WARN) {
        return { KillSwitchLevel::SYMBOL_FREEZE, 0.0, "DRAWDOWN_FREEZE" };
    }
    if (st.consecutive_losses > CONSEC_LOSS_HARD) {
        return { KillSwitchLevel::SYMBOL_FREEZE, 0.0, "CONSEC_LOSS_FREEZE" };
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // LEVEL 3: NO NEW ENTRY - Exit only, no new positions
    // ═══════════════════════════════════════════════════════════════════════
    if (st.ema_latency_ms > LATENCY_WARN_MS * 0.8) {
        return { KillSwitchLevel::NO_NEW_ENTRY, 0.0, "LATENCY_NO_ENTRY" };
    }
    if (st.error_count > ERROR_WARN_COUNT) {
        return { KillSwitchLevel::NO_NEW_ENTRY, 0.0, "ERROR_NO_ENTRY" };
    }
    if (st.consecutive_losses > CONSEC_LOSS_WARN) {
        return { KillSwitchLevel::NO_NEW_ENTRY, 0.0, "CONSEC_LOSS_NO_ENTRY" };
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // LEVEL 2: THROTTLE - Reduce size by 50%
    // ═══════════════════════════════════════════════════════════════════════
    if (st.ema_latency_ms > LATENCY_WARN_MS * 0.6) {
        return { KillSwitchLevel::THROTTLE, 0.5, "LATENCY_THROTTLE" };
    }
    if (st.ema_slippage > SLIPPAGE_WARN * 0.7) {
        return { KillSwitchLevel::THROTTLE, 0.5, "SLIPPAGE_THROTTLE" };
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // LEVEL 1: WARNING - Log only, no action
    // ═══════════════════════════════════════════════════════════════════════
    if (st.ema_latency_ms > LATENCY_WARN_MS * 0.4) {
        return { KillSwitchLevel::WARNING, 1.0, "LATENCY_WARN" };
    }
    if (st.ema_slippage > SLIPPAGE_WARN * 0.4) {
        return { KillSwitchLevel::WARNING, 1.0, "SLIPPAGE_WARN" };
    }
    
    // LEVEL 0: NORMAL - Full operation
    return { KillSwitchLevel::NORMAL, 1.0, "" };
}

// =============================================================================
// EVALUATE RECOVERY - Check if safe to resume
// =============================================================================
inline RecoveryDecision EvaluateRecovery(
    [[maybe_unused]] const char* symbol,
    const RecoveryStats& st
) {
    using namespace KillSwitchThresholds;
    
    // Still cooling down
    if (st.ema_latency_ms > LATENCY_RECOVER_MS ||
        st.ema_slippage > SLIPPAGE_RECOVER) {
        return { false, RecoveryState::COOLING };
    }
    
    // Need more stable ticks
    if (st.stable_ticks < REQUIRED_STABLE_TICKS) {
        return { false, RecoveryState::COOLING };
    }
    
    // Ready to rearm
    return { true, RecoveryState::REARMED };
}

// =============================================================================
// KILL-SWITCH CONTROLLER - Per-symbol state machine
// =============================================================================
class KillSwitchController {
public:
    void update(const char* symbol, const KillSwitchStats& stats, uint64_t now_ns) {
        decision_ = EvaluateKillSwitch(symbol, stats);
        
        // Log state changes
        if (decision_.level != prev_level_) {
            std::cout << "[KILL-SWITCH " << symbol << "] " 
                      << KillSwitchLevelStr(prev_level_) << " → "
                      << KillSwitchLevelStr(decision_.level);
            if (decision_.reason[0] != '\0') {
                std::cout << " (" << decision_.reason << ")";
            }
            std::cout << "\n";
            
            prev_level_ = decision_.level;
            last_change_ns_ = now_ns;
        }
        
        // Handle halt/recovery - Level 4+ triggers halt
        if (decision_.level >= KillSwitchLevel::SYMBOL_FREEZE) {
            recovery_state_ = RecoveryState::HALTED;
            recovery_stats_.reset();
        }
    }
    
    void tickRecovery(double latency_ms, double slippage) {
        if (recovery_state_ == RecoveryState::HALTED ||
            recovery_state_ == RecoveryState::COOLING) {
            recovery_stats_.tick(latency_ms, slippage);
            auto rd = EvaluateRecovery("", recovery_stats_);
            recovery_state_ = rd.state;
            if (rd.allow_trading) {
                recovery_state_ = RecoveryState::RUNNING;
                decision_.level = KillSwitchLevel::NORMAL;
                decision_.risk_multiplier = 1.0;
            }
        }
    }
    
    // Can trade: NORMAL, WARNING, THROTTLE only
    bool canTrade() const {
        return decision_.level <= KillSwitchLevel::THROTTLE;
    }
    
    // Can open new positions: NORMAL, WARNING only
    bool canOpenNew() const {
        return decision_.level <= KillSwitchLevel::WARNING;
    }
    
    double riskMultiplier() const { return decision_.risk_multiplier; }
    KillSwitchLevel level() const { return decision_.level; }
    RecoveryState recoveryState() const { return recovery_state_; }
    const char* reason() const { return decision_.reason; }
    
private:
    KillSwitchDecision decision_;
    KillSwitchLevel prev_level_ = KillSwitchLevel::NORMAL;
    RecoveryState recovery_state_ = RecoveryState::RUNNING;
    RecoveryStats recovery_stats_;
    uint64_t last_change_ns_ = 0;
};

} // namespace Omega
