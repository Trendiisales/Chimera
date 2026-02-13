#include "latency/LatencyRouter.hpp"
#include "latency/LatencyGovernor.hpp"

#include <string>
#include <cstdio>

namespace {

LatencyGovernor& latency_instance() {
    static LatencyGovernor instance;
    return instance;
}

}

/* ============================================================
   PUBLIC API
   ============================================================ */

namespace LatencyRouter {

void recordRtt(double rtt_ms) {
    latency_instance().record_rtt_ms(rtt_ms);
}

bool allowEntry(const std::string& symbol) {
    (void)symbol;

    double cur = latency_instance().current();

    // Block if RTT > 25ms
    if (cur > 25.0)
        return false;

    return true;
}

void dumpStatus() {
    double cur = latency_instance().current();

    std::printf(
        "[LATENCY] cur=%.2f ms\n",
        cur
    );
}

} // namespace LatencyRouter
