#include "TelemetrySinkStdout.hpp"

namespace chimera {

void TelemetrySinkStdout::publish(const LatencyRecord& r) {
    std::cout
        << "[LATENCY]"
        << " sym=" << r.symbol
        << " id=" << r.causal_id
        << " d2s_ns=" << r.decision_to_send_ns()
        << " rtt_ns=" << r.exchange_rtt_ns()
        << " queue_ns=" << r.queue_wait_ns()
        << " d2f_ns=" << r.decision_to_fill_ns()
        << " slip_bps=" << r.slippage_bps()
        << " rejected=" << (r.rejected ? 1 : 0)
        << "\n";
}

}
