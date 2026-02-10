#pragma once

#include "latency/LatencyGovernor.hpp"

// ═══════════════════════════════════════════════════════════════════════════
// LATENCY GOVERNOR LOGGING UTILITIES
// ═══════════════════════════════════════════════════════════════════════════

inline const char* latency_regime_str(LatencyRegime r) {
    switch (r) {
        case LatencyRegime::FAST:     return "FAST";
        case LatencyRegime::NORMAL:   return "NORMAL";
        case LatencyRegime::DEGRADED: return "DEGRADED";
    }
    return "UNKNOWN";
}
