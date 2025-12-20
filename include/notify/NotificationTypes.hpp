#pragma once
#include <cstdint>

namespace Chimera {

enum class NotifyChannel : uint8_t {
    LOCAL_LOG = 1,
    EXTERNAL  = 2
};

struct alignas(64) NotificationEvent {
    uint64_t ts_ns;
    uint16_t code;
    uint8_t  level;
    uint8_t  channel;
};

}
