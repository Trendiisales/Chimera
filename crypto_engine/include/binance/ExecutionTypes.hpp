#pragma once

#include <cstdint>
#include <string>

namespace binance {

enum class Side : uint8_t {
    BUY  = 1,
    SELL = 2,
    FLAT = 3
};

struct ExecutionIntent {
    std::string symbol;
    Side side;
    double price;
    double quantity;
    uint64_t ts_ns;
};

}
