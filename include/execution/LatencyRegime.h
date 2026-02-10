#pragma once
#include <cstdint>

enum class LatencyRegime {
    FAST,
    NORMAL,
    SLOW
};

struct LatencyStats {
    double p50;
    double p90;
    double p95;
    double p99;
};

inline LatencyRegime classify_latency(const LatencyStats& s) {
    if (s.p95 <= 6.0) return LatencyRegime::FAST;
    if (s.p95 <= 12.0) return LatencyRegime::NORMAL;
    return LatencyRegime::SLOW;
}
