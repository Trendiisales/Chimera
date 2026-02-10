#include "latency/ExecutionPolicy.hpp"

bool ExecutionPolicy::allow_entry(const std::string& symbol,
                                  LatencyState s,
                                  double cur) const {
    if (symbol == "XAUUSD") {
        return (s == LatencyState::FAST && cur <= 6.0);
    }
    if (symbol == "XAGUSD") {
        return (s != LatencyState::DEGRADED && cur <= 15.0);
    }
    return false;
}

bool ExecutionPolicy::allow_add_leg(const std::string& symbol,
                                    LatencyState s,
                                    double cur) const {
    if (symbol == "XAUUSD") {
        return (s == LatencyState::FAST && cur <= 5.0);
    }
    if (symbol == "XAGUSD") {
        return (cur <= 12.0);
    }
    return false;
}

bool ExecutionPolicy::force_flat(const std::string& symbol,
                                 double cur) const {
    if (symbol == "XAUUSD")
        return cur > 10.0;
    if (symbol == "XAGUSD")
        return cur > 20.0;
    return false;
}
