#pragma once
#include <cstdint>

namespace chimera {

struct SnapshotHeader {
    uint32_t magic{0x43484D52};   // "CHMR"
    uint32_t version{1};
    uint64_t ts_ns{0};
    uint32_t size{0};
    uint32_t crc{0};
};

}
