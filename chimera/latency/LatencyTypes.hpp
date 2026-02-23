#pragma once
#include <cstdint>
#include <string>

namespace chimera {

enum class ExecEventType : uint8_t {
    SUBMIT = 0,
    ACK    = 1,
    FILL   = 2,
    CANCEL = 3,
    REJECT = 4
};

struct PriceQty {
    double price = 0.0;
    double qty   = 0.0;
};

}
