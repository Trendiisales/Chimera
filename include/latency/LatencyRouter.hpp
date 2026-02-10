#pragma once
#include <string>

namespace LatencyRouter {
    void recordRtt(double rtt_ms);
    bool allowEntry(const std::string& symbol);
    void dumpStatus();
}
