#pragma once
#include <cstdint>
#include <atomic>
#include <string>

namespace Chimera {

enum class LogRecordType : uint16_t {
    TICK = 1,
    ORDER_INTENT = 2,
    EXECUTION = 3,
    VENUE_HEALTH = 4,
    SYSTEM = 5
};

struct alignas(64) BinaryLogHeader {
    uint64_t seq;
    uint64_t ts_ns;
    uint16_t type;
    uint16_t venue;
    uint32_t size;
};

class BinaryLogWriter {
public:
    explicit BinaryLogWriter(const std::string& path);
    ~BinaryLogWriter();
    bool write(const void* data, uint32_t size, LogRecordType type, uint16_t venue);
    uint64_t sequence() const;
private:
    int fd_;
    uint8_t* base_;
    size_t capacity_;
    std::atomic<uint64_t> seq_;
    size_t offset_;
};

}
