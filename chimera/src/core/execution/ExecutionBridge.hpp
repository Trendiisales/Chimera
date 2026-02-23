#pragma once
#include <cstdint>
#include <string>

#include "../control/LatencyFilter.hpp"
#include "../control/CapitalAllocator.hpp"
#include "../control/SignalFusion.hpp"
#include "ExecPolicyEngine.hpp"

namespace chimera {

class ExecutionBridge {
public:
    ExecutionBridge(double max_usd);

    void on_market(double price,
                   double spread,
                   double depth_top,
                   uint64_t ts_ns);

    void on_latency_sample(const LatencySample& s);

private:
    LatencyFilter m_latency;
    ExecPolicyEngine m_policy;
    CapitalAllocator m_allocator;
    SignalFusion m_fusion;

    double m_last_price;
};

}
