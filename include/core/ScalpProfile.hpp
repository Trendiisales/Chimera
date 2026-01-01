#pragma once
// =============================================================================
// CHIMERA SCALP PROFILE SYSTEM - v4.8.0
// =============================================================================
// DUAL-SCALP ARCHITECTURE:
//   CORE       - Rare, structural, big edge (unchanged)
//   SCALP-NY   - Aggressive, momentum + continuation
//   SCALP-LDN  - Controlled, range + breakout scalps
//
// SAME ENGINE. DIFFERENT TOLERANCES.
// =============================================================================

#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>

// Use unified enum definitions (LatencyState)
#include "../shared/ChimeraEnums.hpp"

namespace Chimera {

// =============================================================================
// SESSION CLASSIFICATION (MANDATORY)
// Derived from: venue throughput, volatility percentile, tick rate
// =============================================================================
enum class Session : uint8_t {
    ASIA = 0,
    LONDON = 1,
    NY_OPEN = 2,
    NY_CONTINUATION = 3,
    OFF_HOURS = 4
};

inline const char* sessionToString(Session s) {
    switch (s) {
        case Session::ASIA:            return "ASIA";
        case Session::LONDON:          return "LONDON";
        case Session::NY_OPEN:         return "NY_OPEN";
        case Session::NY_CONTINUATION: return "NY_CONTINUATION";
        case Session::OFF_HOURS:       return "OFF_HOURS";
        default:                       return "UNKNOWN";
    }
}

// =============================================================================
// ACTIVITY PROFILE
// =============================================================================
enum class ActivityProfile : uint8_t {
    CORE = 0,       // Rare, structural, big edge
    SCALP_NY = 1,   // Aggressive, momentum + continuation
    SCALP_LDN = 2,  // Controlled, range + breakout scalps
    DISABLED = 3    // No trading allowed
};

inline const char* profileToString(ActivityProfile p) {
    switch (p) {
        case ActivityProfile::CORE:      return "CORE";
        case ActivityProfile::SCALP_NY:  return "SCALP-NY";
        case ActivityProfile::SCALP_LDN: return "SCALP-LDN";
        case ActivityProfile::DISABLED:  return "DISABLED";
        default:                         return "UNKNOWN";
    }
}

// =============================================================================
// SCALP ENTRY BLOCKER REASONS
// =============================================================================
enum class ScalpBlocker : uint8_t {
    NONE = 0,
    WRONG_SESSION,
    REGIME_TOXIC,
    EDGE_TOO_LOW,
    PERSISTENCE_LOW,
    IMBALANCE_WEAK,
    SPREAD_TOO_WIDE,
    RANGE_EXPANSION,
    LATENCY_NOT_NORMAL,
    SHOCK_ACTIVE,
    DAILY_LOSS_HIT,
    MAX_TRADES_HIT,
    CONSECUTIVE_LOSSES,
    SYMBOL_NOT_ALLOWED,
    PROFILE_DISABLED
};

inline const char* blockerToString(ScalpBlocker b) {
    switch (b) {
        case ScalpBlocker::NONE:               return "READY";
        case ScalpBlocker::WRONG_SESSION:      return "WRONG_SESSION";
        case ScalpBlocker::REGIME_TOXIC:       return "REGIME_TOXIC";
        case ScalpBlocker::EDGE_TOO_LOW:       return "EDGE_TOO_LOW";
        case ScalpBlocker::PERSISTENCE_LOW:    return "PERSISTENCE_LOW";
        case ScalpBlocker::IMBALANCE_WEAK:     return "IMBALANCE_WEAK";
        case ScalpBlocker::SPREAD_TOO_WIDE:    return "SPREAD_TOO_WIDE";
        case ScalpBlocker::RANGE_EXPANSION:    return "RANGE_EXPANSION";
        case ScalpBlocker::LATENCY_NOT_NORMAL: return "LATENCY_NOT_NORMAL";
        case ScalpBlocker::SHOCK_ACTIVE:       return "SHOCK_ACTIVE";
        case ScalpBlocker::DAILY_LOSS_HIT:     return "DAILY_LOSS_HIT";
        case ScalpBlocker::MAX_TRADES_HIT:     return "MAX_TRADES_HIT";
        case ScalpBlocker::CONSECUTIVE_LOSSES: return "CONSECUTIVE_LOSSES";
        case ScalpBlocker::SYMBOL_NOT_ALLOWED: return "SYMBOL_NOT_ALLOWED";
        case ScalpBlocker::PROFILE_DISABLED:   return "PROFILE_DISABLED";
        default:                               return "UNKNOWN";
    }
}

// =============================================================================
// SCALP EXIT REASON
// =============================================================================
enum class ScalpExitReason : uint8_t {
    NONE = 0,
    EDGE_DECAY,
    LATENCY_DEGRADED,
    TIME_CAP,
    RANGE_ADVERSE,
    SHOCK_DETECTED,
    MANUAL_EXIT,
    DAILY_STOP
};

inline const char* exitReasonToString(ScalpExitReason r) {
    switch (r) {
        case ScalpExitReason::NONE:             return "HOLDING";
        case ScalpExitReason::EDGE_DECAY:       return "EDGE_DECAY";
        case ScalpExitReason::LATENCY_DEGRADED: return "LATENCY_DEGRADED";
        case ScalpExitReason::TIME_CAP:         return "TIME_CAP";
        case ScalpExitReason::RANGE_ADVERSE:    return "RANGE_ADVERSE";
        case ScalpExitReason::SHOCK_DETECTED:   return "SHOCK_DETECTED";
        case ScalpExitReason::MANUAL_EXIT:      return "MANUAL_EXIT";
        case ScalpExitReason::DAILY_STOP:       return "DAILY_STOP";
        default:                                return "UNKNOWN";
    }
}

// =============================================================================
// REGIME (for entry filtering)
// =============================================================================
enum class Regime : uint8_t {
    STABLE = 0,
    TRANSITION = 1,
    TRENDING = 2,
    TOXIC = 3
};

// =============================================================================
// SCALP THRESHOLDS - EXACT BASE NUMBERS (NO RANGES, NO TUNING GUESSWORK)
// =============================================================================

// NAS100 SCALP-NY (primary profit engine)
struct NAS100_SCALP_NY {
    static constexpr double base_edge = 0.55;
    static constexpr double persistence_min = 0.40;
    static constexpr double imbalance_min = 0.15;
    static constexpr double time_cap_sec = 3.5;
    static constexpr double edge_decay_exit = 0.70;
    // latency_required = NORMAL only
};

// NAS100 SCALP-LONDON (defensive)
struct NAS100_SCALP_LDN {
    static constexpr double base_edge = 0.65;
    static constexpr double persistence_min = 0.50;
    static constexpr double spread_max_mult = 1.15;  // median_spread * 1.15
    static constexpr double range_cap = 1.80;
    static constexpr double time_cap_sec = 2.5;
    static constexpr double edge_decay_exit = 0.80;
    // latency_required = NORMAL only
};

// XAUUSD SCALP-NY (Gold needs more edge, exits faster)
struct XAUUSD_SCALP_NY {
    static constexpr double base_edge = 0.60;
    static constexpr double persistence_min = 0.45;
    static constexpr double imbalance_min = 0.18;
    static constexpr double time_cap_sec = 3.0;
    static constexpr double edge_decay_exit = 0.75;
    // latency_required = NORMAL only
};

// XAUUSD SCALP-LONDON (Gold lies more than NAS100)
struct XAUUSD_SCALP_LDN {
    static constexpr double base_edge = 0.70;
    static constexpr double persistence_min = 0.55;
    static constexpr double spread_max_mult = 1.10;  // median_spread * 1.10
    static constexpr double range_cap = 1.70;
    static constexpr double time_cap_sec = 2.0;
    static constexpr double edge_decay_exit = 0.80;
    // latency_required = NORMAL only
};

// CORE (unchanged, rare)
struct CORE_PROFILE {
    static constexpr double base_edge = 1.00;
    static constexpr double persistence_min = 0.65;
    static constexpr bool expansion_required = true;
};

// =============================================================================
// DAILY LIMITS (HARD)
// =============================================================================
struct ScalpDailyLimits {
    static constexpr double max_loss_R = -0.50;      // CORE risk unit
    static constexpr int max_trades = 25;
    static constexpr int max_consecutive_losses = 5;
};

struct CoreDailyLimits {
    static constexpr double max_loss_R = -1.00;
};

// =============================================================================
// RISK SCALING
// =============================================================================
struct ScalpRisk {
    static constexpr double scalp_ny_mult = 0.30;   // risk = 0.30 × CORE
    static constexpr double scalp_ldn_mult = 0.20;  // risk = 0.20 × CORE
    static constexpr int max_positions = 1;         // No pyramids in SCALP
};

// =============================================================================
// MARKET STATE INPUT (for entry evaluation)
// =============================================================================
struct ScalpMarketState {
    double edge = 0.0;
    double persistence = 0.0;
    double imbalance = 0.0;
    double spread = 0.0;
    double median_spread = 0.0;
    double range_expansion = 0.0;
    Regime regime = Regime::STABLE;
    LatencyState latency = LatencyState::NORMAL;
    bool shock_active = false;
    bool momentum_burst = false;
    bool imbalance_aligned = false;
    int direction = 0;  // -1 sell, 0 neutral, +1 buy
};

// =============================================================================
// POSITION STATE (for exit evaluation)
// =============================================================================
struct ScalpPosition {
    char symbol[16] = {0};
    int direction = 0;
    double entry_edge = 0.0;
    double entry_price = 0.0;
    double current_price = 0.0;
    uint64_t entry_time_ns = 0;
    bool in_profit = false;
    
    double heldSeconds(uint64_t now_ns) const {
        if (entry_time_ns == 0) return 0.0;
        return static_cast<double>(now_ns - entry_time_ns) / 1e9;
    }
    
    double unrealizedPnlBps() const {
        if (entry_price <= 0.0) return 0.0;
        double pnl = (current_price - entry_price) / entry_price * 10000.0;
        return direction * pnl;
    }
};

// =============================================================================
// SCALP DAILY TRACKER
// =============================================================================
class ScalpDailyTracker {
public:
    static ScalpDailyTracker& instance() {
        static ScalpDailyTracker inst;
        return inst;
    }
    
    void reset() {
        trades_today_.store(0, std::memory_order_relaxed);
        consecutive_losses_.store(0, std::memory_order_relaxed);
        daily_pnl_R_.store(0.0);
        scalp_disabled_.store(false, std::memory_order_relaxed);
    }
    
    void recordTrade(bool win, double pnl_R) {
        trades_today_.fetch_add(1, std::memory_order_relaxed);
        
        // Atomic add for double (approximate)
        double old_pnl = daily_pnl_R_.load();
        while (!daily_pnl_R_.compare_exchange_weak(old_pnl, old_pnl + pnl_R)) {}
        
        if (win) {
            consecutive_losses_.store(0, std::memory_order_relaxed);
        } else {
            consecutive_losses_.fetch_add(1, std::memory_order_relaxed);
        }
        
        checkLimits();
    }
    
    bool isScalpAllowed() const {
        return !scalp_disabled_.load(std::memory_order_acquire);
    }
    
    ScalpBlocker getBlocker() const {
        if (daily_pnl_R_.load() <= ScalpDailyLimits::max_loss_R) {
            return ScalpBlocker::DAILY_LOSS_HIT;
        }
        if (trades_today_.load(std::memory_order_relaxed) >= ScalpDailyLimits::max_trades) {
            return ScalpBlocker::MAX_TRADES_HIT;
        }
        if (consecutive_losses_.load(std::memory_order_relaxed) >= ScalpDailyLimits::max_consecutive_losses) {
            return ScalpBlocker::CONSECUTIVE_LOSSES;
        }
        return ScalpBlocker::NONE;
    }
    
    int tradesToday() const { return trades_today_.load(std::memory_order_relaxed); }
    int consecutiveLosses() const { return consecutive_losses_.load(std::memory_order_relaxed); }
    double dailyPnlR() const { return daily_pnl_R_.load(); }
    
    void printStatus() const {
        printf("[SCALP-TRACKER] Trades=%d/%d ConsecLoss=%d/%d PnL=%.2fR/%.2fR Enabled=%s\n",
               trades_today_.load(), ScalpDailyLimits::max_trades,
               consecutive_losses_.load(), ScalpDailyLimits::max_consecutive_losses,
               daily_pnl_R_.load(), ScalpDailyLimits::max_loss_R,
               isScalpAllowed() ? "YES" : "NO");
    }

private:
    ScalpDailyTracker() { reset(); }
    
    void checkLimits() {
        if (daily_pnl_R_.load() <= ScalpDailyLimits::max_loss_R) {
            printf("\n[SCALP-STOP] ═════════════════════════════════════════════\n");
            printf("[SCALP-STOP] DAILY LOSS LIMIT HIT: %.2fR\n", daily_pnl_R_.load());
            printf("[SCALP-STOP] SCALP DISABLED - CORE UNCHANGED\n");
            printf("[SCALP-STOP] ═════════════════════════════════════════════\n\n");
            scalp_disabled_.store(true, std::memory_order_release);
        }
        
        if (trades_today_.load(std::memory_order_relaxed) >= ScalpDailyLimits::max_trades) {
            printf("\n[SCALP-STOP] MAX TRADES HIT: %d\n", trades_today_.load());
            scalp_disabled_.store(true, std::memory_order_release);
        }
        
        if (consecutive_losses_.load(std::memory_order_relaxed) >= ScalpDailyLimits::max_consecutive_losses) {
            printf("\n[SCALP-STOP] CONSECUTIVE LOSSES HIT: %d\n", consecutive_losses_.load());
            scalp_disabled_.store(true, std::memory_order_release);
        }
    }
    
    std::atomic<int> trades_today_;
    std::atomic<int> consecutive_losses_;
    std::atomic<double> daily_pnl_R_;
    std::atomic<bool> scalp_disabled_;
};

// =============================================================================
// PROFILE SELECTION (AUTHORITATIVE)
// =============================================================================
inline ActivityProfile profileForSymbol(const char* symbol, Session session) {
    // Only NAS100 and XAUUSD allowed in SCALP
    bool is_scalp_symbol = (strcmp(symbol, "NAS100") == 0 || strcmp(symbol, "XAUUSD") == 0);
    
    if (!is_scalp_symbol) {
        return ActivityProfile::CORE;
    }
    
    // Check daily limits
    if (!ScalpDailyTracker::instance().isScalpAllowed()) {
        return ActivityProfile::CORE;  // Fall back to CORE, not DISABLED
    }
    
    // Session-based profile selection
    switch (session) {
        case Session::NY_OPEN:
        case Session::NY_CONTINUATION:
            return ActivityProfile::SCALP_NY;
            
        case Session::LONDON:
            return ActivityProfile::SCALP_LDN;
            
        case Session::ASIA:
        case Session::OFF_HOURS:
        default:
            return ActivityProfile::CORE;  // No SCALP in Asia/Off-hours
    }
}

// =============================================================================
// SCALP ENTRY EVALUATOR
// =============================================================================
class ScalpEntryEvaluator {
public:
    // Returns true if entry allowed, sets blocker if not
    static bool evaluate(
        const char* symbol,
        ActivityProfile profile,
        const ScalpMarketState& state,
        ScalpBlocker* out_blocker = nullptr
    ) {
        // Profile check
        if (profile == ActivityProfile::DISABLED) {
            if (out_blocker) *out_blocker = ScalpBlocker::PROFILE_DISABLED;
            return false;
        }
        
        if (profile == ActivityProfile::CORE) {
            // CORE has different rules - not handled here
            if (out_blocker) *out_blocker = ScalpBlocker::NONE;
            return true;  // Defer to CORE logic
        }
        
        // Daily limits
        if (!ScalpDailyTracker::instance().isScalpAllowed()) {
            if (out_blocker) *out_blocker = ScalpDailyTracker::instance().getBlocker();
            return false;
        }
        
        // Common checks for all SCALP profiles
        if (state.regime == Regime::TOXIC) {
            if (out_blocker) *out_blocker = ScalpBlocker::REGIME_TOXIC;
            return false;
        }
        
        if (state.latency != LatencyState::NORMAL) {
            if (out_blocker) *out_blocker = ScalpBlocker::LATENCY_NOT_NORMAL;
            return false;
        }
        
        if (state.shock_active) {
            if (out_blocker) *out_blocker = ScalpBlocker::SHOCK_ACTIVE;
            return false;
        }
        
        // Symbol-specific thresholds
        bool is_nas100 = (strcmp(symbol, "NAS100") == 0);
        bool is_xauusd = (strcmp(symbol, "XAUUSD") == 0);
        
        if (!is_nas100 && !is_xauusd) {
            if (out_blocker) *out_blocker = ScalpBlocker::SYMBOL_NOT_ALLOWED;
            return false;
        }
        
        // Profile-specific evaluation
        if (profile == ActivityProfile::SCALP_NY) {
            return evaluateScalpNY(symbol, is_nas100, state, out_blocker);
        } else if (profile == ActivityProfile::SCALP_LDN) {
            return evaluateScalpLDN(symbol, is_nas100, state, out_blocker);
        }
        
        if (out_blocker) *out_blocker = ScalpBlocker::PROFILE_DISABLED;
        return false;
    }
    
private:
    static bool evaluateScalpNY(const char* symbol, bool is_nas100, 
                                 const ScalpMarketState& state, ScalpBlocker* out_blocker) {
        (void)symbol;  // Reserved for future symbol-specific tuning
        double edge_min, persistence_min, imbalance_min;
        
        if (is_nas100) {
            edge_min = NAS100_SCALP_NY::base_edge;
            persistence_min = NAS100_SCALP_NY::persistence_min;
            imbalance_min = NAS100_SCALP_NY::imbalance_min;
        } else {
            edge_min = XAUUSD_SCALP_NY::base_edge;
            persistence_min = XAUUSD_SCALP_NY::persistence_min;
            imbalance_min = XAUUSD_SCALP_NY::imbalance_min;
        }
        
        // Edge check
        if (state.edge < edge_min) {
            if (out_blocker) *out_blocker = ScalpBlocker::EDGE_TOO_LOW;
            return false;
        }
        
        // Persistence check
        if (state.persistence < persistence_min) {
            if (out_blocker) *out_blocker = ScalpBlocker::PERSISTENCE_LOW;
            return false;
        }
        
        // Imbalance OR momentum burst required
        bool imbalance_ok = state.imbalance_aligned && std::fabs(state.imbalance) >= imbalance_min;
        if (!imbalance_ok && !state.momentum_burst) {
            if (out_blocker) *out_blocker = ScalpBlocker::IMBALANCE_WEAK;
            return false;
        }
        
        if (out_blocker) *out_blocker = ScalpBlocker::NONE;
        return true;
    }
    
    static bool evaluateScalpLDN(const char* symbol, bool is_nas100,
                                  const ScalpMarketState& state, ScalpBlocker* out_blocker) {
        (void)symbol;  // Reserved for future symbol-specific tuning
        double edge_min, persistence_min, spread_max_mult, range_cap;
        
        if (is_nas100) {
            edge_min = NAS100_SCALP_LDN::base_edge;
            persistence_min = NAS100_SCALP_LDN::persistence_min;
            spread_max_mult = NAS100_SCALP_LDN::spread_max_mult;
            range_cap = NAS100_SCALP_LDN::range_cap;
        } else {
            edge_min = XAUUSD_SCALP_LDN::base_edge;
            persistence_min = XAUUSD_SCALP_LDN::persistence_min;
            spread_max_mult = XAUUSD_SCALP_LDN::spread_max_mult;
            range_cap = XAUUSD_SCALP_LDN::range_cap;
        }
        
        // Regime must be STABLE or TRANSITION
        if (state.regime != Regime::STABLE && state.regime != Regime::TRANSITION) {
            if (out_blocker) *out_blocker = ScalpBlocker::REGIME_TOXIC;
            return false;
        }
        
        // Edge check
        if (state.edge < edge_min) {
            if (out_blocker) *out_blocker = ScalpBlocker::EDGE_TOO_LOW;
            return false;
        }
        
        // Persistence check
        if (state.persistence < persistence_min) {
            if (out_blocker) *out_blocker = ScalpBlocker::PERSISTENCE_LOW;
            return false;
        }
        
        // Spread check
        double spread_max = state.median_spread * spread_max_mult;
        if (state.spread > spread_max) {
            if (out_blocker) *out_blocker = ScalpBlocker::SPREAD_TOO_WIDE;
            return false;
        }
        
        // Range expansion check
        if (state.range_expansion >= range_cap) {
            if (out_blocker) *out_blocker = ScalpBlocker::RANGE_EXPANSION;
            return false;
        }
        
        if (out_blocker) *out_blocker = ScalpBlocker::NONE;
        return true;
    }
};

// =============================================================================
// SCALP EXIT EVALUATOR
// =============================================================================
class ScalpExitEvaluator {
public:
    static bool shouldExit(
        const char* symbol,
        ActivityProfile profile,
        const ScalpPosition& pos,
        const ScalpMarketState& state,
        uint64_t now_ns,
        ScalpExitReason* out_reason = nullptr
    ) {
        if (profile == ActivityProfile::CORE) {
            if (out_reason) *out_reason = ScalpExitReason::NONE;
            return false;  // CORE has different exit rules
        }
        
        bool is_nas100 = (strcmp(symbol, "NAS100") == 0);
        
        if (profile == ActivityProfile::SCALP_NY) {
            return evaluateExitNY(symbol, is_nas100, pos, state, now_ns, out_reason);
        } else if (profile == ActivityProfile::SCALP_LDN) {
            return evaluateExitLDN(symbol, is_nas100, pos, state, now_ns, out_reason);
        }
        
        if (out_reason) *out_reason = ScalpExitReason::NONE;
        return false;
    }
    
private:
    static bool evaluateExitNY(const char* symbol, bool is_nas100,
                                const ScalpPosition& pos, const ScalpMarketState& state,
                                uint64_t now_ns, ScalpExitReason* out_reason) {
        (void)symbol;  // Reserved for future symbol-specific tuning
        double edge_decay_thresh, time_cap;
        
        if (is_nas100) {
            edge_decay_thresh = NAS100_SCALP_NY::edge_decay_exit;
            time_cap = NAS100_SCALP_NY::time_cap_sec;
        } else {
            edge_decay_thresh = XAUUSD_SCALP_NY::edge_decay_exit;
            time_cap = XAUUSD_SCALP_NY::time_cap_sec;
        }
        
        // Edge decay < 70% of entry
        if (pos.entry_edge > 0 && state.edge < pos.entry_edge * edge_decay_thresh) {
            if (out_reason) *out_reason = ScalpExitReason::EDGE_DECAY;
            return true;
        }
        
        // Latency != NORMAL → EXIT
        if (state.latency != LatencyState::NORMAL) {
            if (out_reason) *out_reason = ScalpExitReason::LATENCY_DEGRADED;
            return true;
        }
        
        // Time > 3.5s & not profitable
        double held_sec = pos.heldSeconds(now_ns);
        if (held_sec > time_cap && !pos.in_profit) {
            if (out_reason) *out_reason = ScalpExitReason::TIME_CAP;
            return true;
        }
        
        // Shock → Immediate
        if (state.shock_active) {
            if (out_reason) *out_reason = ScalpExitReason::SHOCK_DETECTED;
            return true;
        }
        
        if (out_reason) *out_reason = ScalpExitReason::NONE;
        return false;
    }
    
    static bool evaluateExitLDN(const char* symbol, bool is_nas100,
                                 const ScalpPosition& pos, const ScalpMarketState& state,
                                 uint64_t now_ns, ScalpExitReason* out_reason) {
        (void)symbol;  // Reserved for future symbol-specific tuning
        double edge_decay_thresh, time_cap, range_adverse_cap;
        
        if (is_nas100) {
            edge_decay_thresh = NAS100_SCALP_LDN::edge_decay_exit;
            time_cap = NAS100_SCALP_LDN::time_cap_sec;
            range_adverse_cap = 2.0;  // London range adverse threshold
        } else {
            edge_decay_thresh = XAUUSD_SCALP_LDN::edge_decay_exit;
            time_cap = XAUUSD_SCALP_LDN::time_cap_sec;
            range_adverse_cap = 2.0;
        }
        
        // Edge decay < 80% of entry (tighter than NY)
        if (pos.entry_edge > 0 && state.edge < pos.entry_edge * edge_decay_thresh) {
            if (out_reason) *out_reason = ScalpExitReason::EDGE_DECAY;
            return true;
        }
        
        // Latency != NORMAL → EXIT
        if (state.latency != LatencyState::NORMAL) {
            if (out_reason) *out_reason = ScalpExitReason::LATENCY_DEGRADED;
            return true;
        }
        
        // Range expansion > 2.0 adverse
        bool range_adverse = (state.range_expansion > range_adverse_cap) && 
                             (state.direction != pos.direction);
        if (range_adverse) {
            if (out_reason) *out_reason = ScalpExitReason::RANGE_ADVERSE;
            return true;
        }
        
        // Time > 2.5s & not profitable (tighter than NY)
        double held_sec = pos.heldSeconds(now_ns);
        if (held_sec > time_cap && !pos.in_profit) {
            if (out_reason) *out_reason = ScalpExitReason::TIME_CAP;
            return true;
        }
        
        // Shock → Immediate
        if (state.shock_active) {
            if (out_reason) *out_reason = ScalpExitReason::SHOCK_DETECTED;
            return true;
        }
        
        if (out_reason) *out_reason = ScalpExitReason::NONE;
        return false;
    }
};

// =============================================================================
// OBSERVABILITY - "WHY NOT TRADING" DIAGNOSTICS
// =============================================================================
class ScalpDiagnostics {
public:
    static void printStatus(
        const char* symbol,
        Session session,
        ActivityProfile profile,
        const ScalpMarketState& state,
        ScalpBlocker blocker
    ) {
        printf("\n╔════════════════════════════════════════════════════════════╗\n");
        printf("║  SCALP STATUS                                              ║\n");
        printf("╠════════════════════════════════════════════════════════════╣\n");
        printf("║  SYMBOL:  %-12s                                     ║\n", symbol);
        printf("║  SESSION: %-16s                                 ║\n", sessionToString(session));
        printf("║  PROFILE: %-12s                                     ║\n", profileToString(profile));
        printf("╠════════════════════════════════════════════════════════════╣\n");
        
        // Thresholds depend on profile and symbol
        bool is_nas100 = (strcmp(symbol, "NAS100") == 0);
        double edge_req = 0.0, persistence_req = 0.0;
        
        if (profile == ActivityProfile::SCALP_NY) {
            edge_req = is_nas100 ? NAS100_SCALP_NY::base_edge : XAUUSD_SCALP_NY::base_edge;
            persistence_req = is_nas100 ? NAS100_SCALP_NY::persistence_min : XAUUSD_SCALP_NY::persistence_min;
        } else if (profile == ActivityProfile::SCALP_LDN) {
            edge_req = is_nas100 ? NAS100_SCALP_LDN::base_edge : XAUUSD_SCALP_LDN::base_edge;
            persistence_req = is_nas100 ? NAS100_SCALP_LDN::persistence_min : XAUUSD_SCALP_LDN::persistence_min;
        }
        
        const char* edge_status = state.edge >= edge_req ? "✔" : "✖";
        const char* pers_status = state.persistence >= persistence_req ? "✔" : "✖";
        const char* lat_status = state.latency == LatencyState::NORMAL ? "✔" : "✖";
        const char* shock_status = !state.shock_active ? "✔" : "✖";
        
        printf("║  Edge:        %.2f / %.2f %s                              ║\n", 
               state.edge, edge_req, edge_status);
        printf("║  Persistence: %.2f / %.2f %s                              ║\n", 
               state.persistence, persistence_req, pers_status);
        printf("║  Latency:     %s %s                                       ║\n",
               state.latency == LatencyState::NORMAL ? "NORMAL" : 
               (state.latency == LatencyState::ELEVATED ? "ELEVATED" : "DEGRADED"), lat_status);
        printf("║  Shock:       %s %s                                       ║\n",
               state.shock_active ? "ACTIVE" : "CLEAR", shock_status);
        
        if (profile == ActivityProfile::SCALP_LDN) {
            double spread_max = state.median_spread * (is_nas100 ? NAS100_SCALP_LDN::spread_max_mult 
                                                                  : XAUUSD_SCALP_LDN::spread_max_mult);
            double range_cap = is_nas100 ? NAS100_SCALP_LDN::range_cap : XAUUSD_SCALP_LDN::range_cap;
            const char* spread_status = state.spread <= spread_max ? "✔" : "✖";
            const char* range_status = state.range_expansion < range_cap ? "✔" : "✖";
            
            printf("║  Spread:      %.2f / %.2f %s                            ║\n",
                   state.spread, spread_max, spread_status);
            printf("║  Range:       %.2f / %.2f %s                            ║\n",
                   state.range_expansion, range_cap, range_status);
        }
        
        printf("╠════════════════════════════════════════════════════════════╣\n");
        if (blocker == ScalpBlocker::NONE) {
            printf("║  STATUS: ✔ READY TO TRADE                                  ║\n");
        } else {
            printf("║  BLOCKER: %-20s ✖                          ║\n", blockerToString(blocker));
        }
        printf("╚════════════════════════════════════════════════════════════╝\n\n");
    }
    
    static void printDailyStatus() {
        auto& tracker = ScalpDailyTracker::instance();
        printf("\n[SCALP-DAILY] Trades=%d/%d ConsecLoss=%d/%d PnL=%.2fR/%.2fR Enabled=%s\n",
               tracker.tradesToday(), ScalpDailyLimits::max_trades,
               tracker.consecutiveLosses(), ScalpDailyLimits::max_consecutive_losses,
               tracker.dailyPnlR(), ScalpDailyLimits::max_loss_R,
               tracker.isScalpAllowed() ? "YES" : "NO");
    }
};

// =============================================================================
// RISK CALCULATOR
// =============================================================================
inline double scalpRiskMultiplier(ActivityProfile profile) {
    switch (profile) {
        case ActivityProfile::SCALP_NY:  return ScalpRisk::scalp_ny_mult;
        case ActivityProfile::SCALP_LDN: return ScalpRisk::scalp_ldn_mult;
        case ActivityProfile::CORE:      return 1.0;
        default:                         return 0.0;
    }
}

// =============================================================================
// CONVENIENCE FUNCTIONS
// =============================================================================
inline ScalpDailyTracker& getScalpTracker() { return ScalpDailyTracker::instance(); }

inline void resetScalpDay() { 
    ScalpDailyTracker::instance().reset(); 
    printf("[SCALP] Daily counters reset\n");
}

} // namespace Chimera
