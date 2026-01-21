#pragma once

#include <fstream>
#include <string>
#include <cstdint>
#include <chrono>

namespace chimera {

enum class EventType : uint8_t {
    TICK = 1,
    DECISION = 2,
    ORDER = 3,
    FILL = 4,
    PNL = 5,
    DISCONNECT = 6,
    RECONNECT = 7
};

struct EventHeader {
    uint64_t ts_ns;
    EventType type;
    uint32_t size;
};

class BinaryEventLog {
public:
    explicit BinaryEventLog(
        const std::string& path
    );

    ~BinaryEventLog();

    void log(
        EventType type,
        const void* data,
        uint32_t size
    );

    void logWithTimestamp(
        EventType type,
        uint64_t ts_ns,
        const void* data,
        uint32_t size
    );

private:
    std::ofstream out;
};

}
