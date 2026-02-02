#include "execution/ExecPolicy.hpp"

namespace chimera {

ExecPolicyEngine::ExecPolicyEngine()
    : m_latency_thresh_ns(5000000.0),
      m_edge_thresh(1.2) {}

ExecPolicy ExecPolicyEngine::decide(double edge_score,
                                    double latency_ns,
                                    double spread_bps,
                                    double depth) {
    ExecPolicy p;

    if (edge_score > m_edge_thresh && latency_ns < m_latency_thresh_ns) {
        p.mode = TAKER;
        p.slice_size = depth * 0.25;
        p.price_offset = 0.0;
        p.repost_ns = 0;
    } else {
        p.mode = MAKER;
        p.slice_size = depth * 0.10;
        p.price_offset = spread_bps * 0.5;
        p.repost_ns = 20000000;
    }

    return p;
}

}
