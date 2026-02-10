#pragma once
#include "latency/LatencyMonitor.hpp"

enum class LatencyState {
    FAST,
    NORMAL,
    DEGRADED
};

class LatencyClassifier {
public:
    LatencyClassifier();

    LatencyState update(const LatencyMonitor& m);
    LatencyState state() const;

private:
    LatencyState state_;
    int stable_ticks_;
};
