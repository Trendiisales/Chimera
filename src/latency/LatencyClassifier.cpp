#include "latency/LatencyClassifier.hpp"

LatencyClassifier::LatencyClassifier()
    : state_(LatencyState::DEGRADED), stable_ticks_(0) {}

LatencyState LatencyClassifier::state() const {
    return state_;
}

LatencyState LatencyClassifier::update(const LatencyMonitor& m) {
    LatencyState next = state_;

    // HARD REALITY FIRST
    if (m.current() > 14.0)
        next = LatencyState::DEGRADED;
    else if (m.current() <= 6.0 && m.ewma() <= 6.0)
        next = LatencyState::FAST;
    else
        next = LatencyState::NORMAL;

    // HYSTERESIS: require 10 consecutive confirmations
    if (next == state_) {
        stable_ticks_++;
    } else {
        stable_ticks_ = 0;
    }

    if (stable_ticks_ >= 10) {
        state_ = next;
        stable_ticks_ = 0;
    }

    return state_;
}
