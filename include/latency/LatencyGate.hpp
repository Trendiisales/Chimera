// ═══════════════════════════════════════════════════════════════════════════════
// include/latency/LatencyGate.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Latency-aware trade gating and edge adjustment
// OWNER: Jo
// LAST VERIFIED: 2025-01-02
//
// v4.9.10: INSTITUTIONAL LATENCY GATE WITH BOOTSTRAP SUPPORT
//   - Integrates with LatencyBootstrapper for probe-based warmup
//   - BYPASS mode during BOOTSTRAP (never deadlocks)
//   - Block trades during degraded latency (CPU contention, GC, VPS noise)
//   - Adjust required edge based on latency (tighter when fast, looser when slow)
//   - Force TAKER-ONLY mode when latency too high for maker
//
// BOOTSTRAP BEHAVIOR:
//   - During BOOTSTRAP: Allow ALL trades (latency gate bypassed)
//   - During LIVE: Normal latency-aware gating
//   - This prevents the chicken-egg deadlock
//
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstring>
#include "latency/HotPathLatencyTracker.hpp"
#include "runtime/SystemMode.hpp"
#include "bootstrap/LatencyBootstrapper.hpp"

namespace Chimera {

// ─────────────────────────────────────────────────────────────────────────────
// Hot-Path Latency State Classification (renamed to avoid ChimeraEnums conflict)
// ─────────────────────────────────────────────────────────────────────────────
enum class HotPathState : uint8_t {
    FAST       = 0,  // p10 < 0.30ms - aggressive trading
    NORMAL     = 1,  // p10 < 0.60ms - normal trading
    SLOW       = 2,  // p10 < 1.00ms - defensive trading
    DEGRADED   = 3,  // p10 >= 1.00ms or failed checks - NO TRADING
    UNKNOWN    = 4,  // No samples yet
    BOOTSTRAP  = 5   // Bootstrap mode - latency gate bypassed
};

inline const char* hotPathStateStr(HotPathState state) noexcept {
    switch (state) {
        case HotPathState::FAST:      return "FAST";
        case HotPathState::NORMAL:    return "NORMAL";
        case HotPathState::SLOW:      return "SLOW";
        case HotPathState::DEGRADED:  return "DEGRADED";
        case HotPathState::UNKNOWN:   return "UNKNOWN";
        case HotPathState::BOOTSTRAP: return "BOOTSTRAP";
        default:                      return "???";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Execution Mode
// ─────────────────────────────────────────────────────────────────────────────
enum class ExecMode : uint8_t {
    MAKER_FIRST  = 0,  // Try limit order first, fallback to market
    TAKER_ONLY   = 1,  // Market orders only (latency too high for maker)
    NO_TRADE     = 2   // Block all trades
};

inline const char* execModeStr(ExecMode mode) noexcept {
    switch (mode) {
        case ExecMode::MAKER_FIRST: return "MAKER_FIRST";
        case ExecMode::TAKER_ONLY:  return "TAKER_ONLY";
        case ExecMode::NO_TRADE:    return "NO_TRADE";
        default:                    return "???";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Latency Gate Result
// ─────────────────────────────────────────────────────────────────────────────
struct LatencyGateResult {
    bool allowed = false;
    HotPathState state = HotPathState::UNKNOWN;
    ExecMode exec_mode = ExecMode::NO_TRADE;
    double required_edge_bps = 999.0;  // Edge required to pass gate
    char block_reason[32] = {0};
    bool bootstrap_bypass = false;     // True if bypassed due to bootstrap mode
    
    // Latency metrics at decision time
    double min_ms = 0.0;
    double p10_ms = 0.0;
    double p50_ms = 0.0;
    
    void setBlockReason(const char* reason) noexcept {
        strncpy(block_reason, reason, 31);
        block_reason[31] = '\0';
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Latency Gate Configuration
// ─────────────────────────────────────────────────────────────────────────────
struct LatencyGateConfig {
    // Latency thresholds (milliseconds)
    double p10_fast_threshold_ms    = 0.30;   // Below this = FAST
    double p10_normal_threshold_ms  = 0.60;   // Below this = NORMAL
    double p10_slow_threshold_ms    = 1.00;   // Below this = SLOW, above = DEGRADED
    double p50_max_threshold_ms     = 3.00;   // System stability check
    
    // Edge requirements by latency state (basis points)
    double edge_fast_bps            = 6.0;    // Aggressive when fast
    double edge_normal_bps          = 10.0;   // Standard edge
    double edge_slow_bps            = 18.0;   // Defensive when slow
    
    // Maker routing threshold
    double maker_viable_p10_ms      = 0.40;   // Above this = TAKER_ONLY
    
    // Minimum samples before trading
    size_t min_samples              = 0;      // Allow trades during bootstrap, gate kicks in after data
    
    // Sanity checks
    bool require_min_above_zero     = true;   // min_ms must be > 0
};

// ─────────────────────────────────────────────────────────────────────────────
// LatencyGate - Latency-aware trade gating
// ─────────────────────────────────────────────────────────────────────────────
class LatencyGate {
public:
    explicit LatencyGate(const LatencyGateConfig& config = LatencyGateConfig{}) noexcept
        : config_(config)
        , gates_checked_(0)
        , gates_passed_(0)
        , gates_blocked_(0)
        , forced_taker_count_(0)
    {}
    
    // ═══════════════════════════════════════════════════════════════════════
    // MAIN GATE CHECK - Call before EVERY entry
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] LatencyGateResult check(const HotPathLatencyTracker::LatencySnapshot& snap) noexcept {
        ++gates_checked_;
        
        LatencyGateResult result;
        result.min_ms = snap.min_ms();
        result.p10_ms = snap.p10_ms();
        result.p50_ms = snap.p50_ms();
        
        // ─────────────────────────────────────────────────────────────────────
        // BOOTSTRAP BYPASS: If system is in BOOTSTRAP mode, allow ALL trades
        // This is THE FIX for the chicken-egg deadlock
        // ─────────────────────────────────────────────────────────────────────
        if (getSystemMode().isBootstrap()) {
            result.allowed = true;
            result.state = HotPathState::BOOTSTRAP;
            result.exec_mode = ExecMode::TAKER_ONLY;  // Conservative during bootstrap
            result.required_edge_bps = 0.0;  // NO edge requirement during bootstrap
            result.bootstrap_bypass = true;
            ++gates_passed_;
            return result;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Check 1: Minimum samples requirement (only in LIVE mode)
        // ─────────────────────────────────────────────────────────────────────
        if (snap.sample_count < config_.min_samples && config_.min_samples > 0) {
            result.allowed = false;
            result.state = HotPathState::UNKNOWN;
            result.exec_mode = ExecMode::NO_TRADE;
            result.setBlockReason("INSUFFICIENT_SAMPLES");
            ++gates_blocked_;
            return result;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Check 2: Sanity check - min must be > 0 (ONLY if we have samples)
        // ─────────────────────────────────────────────────────────────────────
        if (snap.sample_count > 0 && config_.require_min_above_zero && snap.min_ms() <= 0.0) {
            result.allowed = false;
            result.state = HotPathState::DEGRADED;
            result.exec_mode = ExecMode::NO_TRADE;
            result.setBlockReason("ZERO_LATENCY_SANITY");
            ++gates_blocked_;
            return result;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // FALLBACK: If no samples and somehow in LIVE mode, allow trading
        // (should not happen with proper bootstrap, but safety net)
        // ─────────────────────────────────────────────────────────────────────
        if (snap.sample_count == 0) {
            result.allowed = true;
            result.state = HotPathState::UNKNOWN;
            result.exec_mode = ExecMode::TAKER_ONLY;
            result.required_edge_bps = 0.0;
            ++gates_passed_;
            return result;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Check 3: System stability (p50)
        // ─────────────────────────────────────────────────────────────────────
        if (snap.p50_ms() > config_.p50_max_threshold_ms) {
            result.allowed = false;
            result.state = HotPathState::DEGRADED;
            result.exec_mode = ExecMode::NO_TRADE;
            result.setBlockReason("P50_UNSTABLE");
            ++gates_blocked_;
            return result;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Check 4: Engine responsiveness (p10)
        // ─────────────────────────────────────────────────────────────────────
        if (snap.p10_ms() > config_.p10_slow_threshold_ms) {
            result.allowed = false;
            result.state = HotPathState::DEGRADED;
            result.exec_mode = ExecMode::NO_TRADE;
            result.setBlockReason("P10_DEGRADED");
            ++gates_blocked_;
            return result;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Classify latency state and set edge requirements
        // ─────────────────────────────────────────────────────────────────────
        if (snap.p10_ms() < config_.p10_fast_threshold_ms) {
            result.state = HotPathState::FAST;
            result.required_edge_bps = config_.edge_fast_bps;
        } else if (snap.p10_ms() < config_.p10_normal_threshold_ms) {
            result.state = HotPathState::NORMAL;
            result.required_edge_bps = config_.edge_normal_bps;
        } else {
            result.state = HotPathState::SLOW;
            result.required_edge_bps = config_.edge_slow_bps;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Determine execution mode
        // ─────────────────────────────────────────────────────────────────────
        if (snap.p10_ms() > config_.maker_viable_p10_ms) {
            result.exec_mode = ExecMode::TAKER_ONLY;
            ++forced_taker_count_;
        } else {
            result.exec_mode = ExecMode::MAKER_FIRST;
        }
        
        result.allowed = true;
        ++gates_passed_;
        return result;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // EDGE-ADJUSTED CHECK - Returns if trade should proceed given edge
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] LatencyGateResult checkWithEdge(
        const HotPathLatencyTracker::LatencySnapshot& snap,
        double gross_edge_bps
    ) noexcept {
        LatencyGateResult result = check(snap);
        
        // BOOTSTRAP BYPASS: Skip edge check entirely during bootstrap
        if (result.bootstrap_bypass) {
            return result;  // Already allowed with required_edge_bps = 0
        }
        
        if (result.allowed) {
            // Check if gross edge meets latency-adjusted requirement
            if (gross_edge_bps < result.required_edge_bps) {
                result.allowed = false;
                result.setBlockReason("EDGE_BELOW_LATENCY_REQ");
                --gates_passed_;  // Undo the pass count
                ++gates_blocked_;
            }
        }
        
        return result;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // CONFIGURATION
    // ═══════════════════════════════════════════════════════════════════════
    
    void setConfig(const LatencyGateConfig& config) noexcept {
        config_ = config;
    }
    
    const LatencyGateConfig& config() const noexcept {
        return config_;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // STATS
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] uint64_t gatesChecked() const noexcept { return gates_checked_; }
    [[nodiscard]] uint64_t gatesPassed() const noexcept { return gates_passed_; }
    [[nodiscard]] uint64_t gatesBlocked() const noexcept { return gates_blocked_; }
    [[nodiscard]] uint64_t forcedTakerCount() const noexcept { return forced_taker_count_; }
    
    [[nodiscard]] double passRate() const noexcept {
        if (gates_checked_ == 0) return 0.0;
        return static_cast<double>(gates_passed_) / static_cast<double>(gates_checked_);
    }
    
    void resetStats() noexcept {
        gates_checked_ = 0;
        gates_passed_ = 0;
        gates_blocked_ = 0;
        forced_taker_count_ = 0;
    }
    
private:
    LatencyGateConfig config_;
    uint64_t gates_checked_;
    uint64_t gates_passed_;
    uint64_t gates_blocked_;
    uint64_t forced_taker_count_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Global Latency Gate singleton (for easy access from strategies)
// ─────────────────────────────────────────────────────────────────────────────
inline LatencyGate& getLatencyGate() noexcept {
    static LatencyGate instance;
    return instance;
}

// ─────────────────────────────────────────────────────────────────────────────
// Preset configurations for different trading styles
// ─────────────────────────────────────────────────────────────────────────────

// Ultra-aggressive HFT (requires very fast latency)
inline LatencyGateConfig latencyConfigHFT() noexcept {
    LatencyGateConfig cfg;
    cfg.p10_fast_threshold_ms = 0.20;
    cfg.p10_normal_threshold_ms = 0.40;
    cfg.p10_slow_threshold_ms = 0.60;
    cfg.p50_max_threshold_ms = 1.50;
    cfg.edge_fast_bps = 4.0;
    cfg.edge_normal_bps = 8.0;
    cfg.edge_slow_bps = 15.0;
    cfg.maker_viable_p10_ms = 0.25;
    cfg.min_samples = 0;  // Allow bootstrap
    return cfg;
}

// Standard scalping (balanced)
inline LatencyGateConfig latencyConfigScalp() noexcept {
    LatencyGateConfig cfg;
    cfg.p10_fast_threshold_ms = 0.30;
    cfg.p10_normal_threshold_ms = 0.60;
    cfg.p10_slow_threshold_ms = 1.00;
    cfg.p50_max_threshold_ms = 3.00;
    cfg.edge_fast_bps = 6.0;
    cfg.edge_normal_bps = 10.0;
    cfg.edge_slow_bps = 18.0;
    cfg.maker_viable_p10_ms = 0.40;
    cfg.min_samples = 0;  // Allow bootstrap
    return cfg;
}

// Relaxed swing trading (more tolerant of latency)
inline LatencyGateConfig latencyConfigSwing() noexcept {
    LatencyGateConfig cfg;
    cfg.p10_fast_threshold_ms = 1.00;
    cfg.p10_normal_threshold_ms = 2.00;
    cfg.p10_slow_threshold_ms = 5.00;
    cfg.p50_max_threshold_ms = 10.00;
    cfg.edge_fast_bps = 15.0;
    cfg.edge_normal_bps = 25.0;
    cfg.edge_slow_bps = 40.0;
    cfg.maker_viable_p10_ms = 2.00;
    cfg.min_samples = 0;  // Allow bootstrap
    return cfg;
}

} // namespace Chimera
