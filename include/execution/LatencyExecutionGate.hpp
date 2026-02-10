#pragma once

#include <string>
#include <cstdio>
#include "latency/LatencyGovernor.hpp"
#include "latency/LatencyGovernorLog.hpp"

// ═══════════════════════════════════════════════════════════════════════════
// LATENCY EXECUTION GATE - Enforcement Point
// ═══════════════════════════════════════════════════════════════════════════
//
// This is the single enforcement point for latency-aware execution.
// All entry and exit decisions flow through this gate.
//
// USAGE:
//   1. Create global governor and gate:
//      static LatencyGovernor g_latency;
//      static LatencyExecutionGate g_gate(g_latency);
//
//   2. Record RTT on every FIX event:
//      g_latency.record_rtt_ms(fix_rtt_ms);
//
//   3. Check before entry:
//      if (!g_gate.allow_entry(symbol)) return;
//
//   4. Check before TIME exit:
//      if (!g_gate.allow_time_exit(symbol)) return;
//
// ═══════════════════════════════════════════════════════════════════════════

class LatencyExecutionGate {
public:
    explicit LatencyExecutionGate(LatencyGovernor& governor)
        : governor_(governor) {}

    // Entry gate - call before submitting ANY entry order
    bool allow_entry(const std::string& symbol) const {
        if (!governor_.allow_entry(symbol)) {
            log_block(symbol, "ENTRY_BLOCKED");
            return false;
        }
        return true;
    }

    // TIME exit gate - call before TIME-based exits only
    // (TP/SL exits are always allowed)
    bool allow_time_exit(const std::string& symbol) const {
        if (!governor_.allow_time_exit(symbol)) {
            log_block(symbol, "TIME_EXIT_BLOCKED");
            return false;
        }
        return true;
    }

private:
    LatencyGovernor& governor_;

    void log_block(const std::string& symbol, const char* reason) const {
        const LatencyRegime r = governor_.regime();
        std::printf(
            "[LATENCY] %s %s regime=%s p95=%.2f p99=%.2f cur=%.2f\n",
            reason,
            symbol.c_str(),
            latency_regime_str(r),
            governor_.p95(),
            governor_.p99(),
            governor_.current()
        );
    }
};
