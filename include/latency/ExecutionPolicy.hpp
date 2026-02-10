#pragma once
#include <string>
#include "latency/LatencyClassifier.hpp"

class ExecutionPolicy {
public:
    bool allow_entry(const std::string& symbol,
                     LatencyState s,
                     double cur_rtt) const;

    bool allow_add_leg(const std::string& symbol,
                       LatencyState s,
                       double cur_rtt) const;

    bool force_flat(const std::string& symbol,
                    double cur_rtt) const;
};
