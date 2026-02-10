#pragma once
#include <cstdint>

struct LatencyStats {
    double p50;
    double p90;
    double p95;
    double p99;

    inline bool is_fast() const {
        return p50 <= 4.0 && p90 <= 5.5;
    }
};
