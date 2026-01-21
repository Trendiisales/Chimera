#include "chimera/audit/BinaryEventLog.hpp"

namespace chimera {

BinaryEventLog::BinaryEventLog(
    const std::string& path
) {
    out.open(
        path,
        std::ios::binary |
        std::ios::out
    );
}

BinaryEventLog::~BinaryEventLog() {
    if (out.is_open()) {
        out.close();
    }
}

void BinaryEventLog::log(
    EventType type,
    const void* data,
    uint32_t size
) {
    auto now = std::chrono::steady_clock::now();
    auto ns = std::chrono::duration_cast<
        std::chrono::nanoseconds
    >(now.time_since_epoch()).count();
    
    logWithTimestamp(type, ns, data, size);
}

void BinaryEventLog::logWithTimestamp(
    EventType type,
    uint64_t ts_ns,
    const void* data,
    uint32_t size
) {
    if (!out.is_open()) return;

    EventHeader hdr;
    hdr.ts_ns = ts_ns;
    hdr.type = type;
    hdr.size = size;

    out.write(
        reinterpret_cast<const char*>(&hdr),
        sizeof(hdr)
    );

    out.write(
        reinterpret_cast<const char*>(data),
        size
    );

    out.flush();
}

}
