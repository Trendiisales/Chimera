#pragma once
#include <string>
#include <cstdint>

namespace chimera {

struct FixTelemetry {
    std::string symbol;
    std::string side;
    double notional;
    uint64_t client_order_id;
    uint64_t send_ts_ns;
    uint64_t ack_ts_ns;
};

}
