#pragma once
#include <cstdint>
#include <string>
#include "execution/LatencyRegime.h"

enum class EntryClass {
    NONE,
    STRONG_IMPULSE,
    WEAK_IMPULSE,
    DRIFT
};

struct EntryDecision {
    bool allow;
    EntryClass cls;
    double size_mult;
    double tp_mult;
    const char* reason;
};

// Entry decision function
EntryDecision decide_entry(
    double velocity,
    double spread,
    const LatencyStats& latency,
    uint64_t now_ns
);
