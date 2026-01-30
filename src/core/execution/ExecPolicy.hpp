#pragma once
#include <string>
#include <cstdint>

namespace chimera {

enum ExecMode {
    MAKER,
    TAKER
};

struct ExecPolicy {
    ExecMode mode = MAKER;
    double slice_size = 0.0;
    double price_offset = 0.0;
    uint64_t repost_ns = 0;
};

class ExecPolicyEngine {
public:
    ExecPolicyEngine();

    ExecPolicy decide(double edge_score,
                      double latency_ns,
                      double spread_bps,
                      double depth);

private:
    double m_latency_thresh_ns;
    double m_edge_thresh;
};

}
