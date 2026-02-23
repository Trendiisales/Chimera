#pragma once
#include <cstdint>
#include <string>

#include "../control/LatencyFilter.hpp"
#include "../control/CapitalAllocator.hpp"
#include "../control/SignalFusion.hpp"
#include "ExecPolicyEngine.hpp"
#include "../fix/FixAdapter.hpp"
#include "../telemetry/FixTelemetry.hpp"

namespace chimera {

class ExecutionBridgeFix {
public:
    ExecutionBridgeFix(double max_usd, FixAdapter* fix);

    void on_market(double price,
                   double spread,
                   double depth_top,
                   uint64_t ts_ns);

    // Call this from your FIX ExecutionReport handler
    void on_execution_report(uint64_t client_order_id,
                             uint64_t ack_ts_ns);

    // Feed latency samples if you already track RX/DECISION/SEND
    void on_latency_sample(const LatencySample& s);

private:
    LatencyFilter m_latency;
    ExecPolicyEngine m_policy;
    CapitalAllocator m_allocator;
    SignalFusion m_fusion;
    FixAdapter* m_fix;

    uint64_t m_client_id_seq;
};

}
