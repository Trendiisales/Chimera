#pragma once
#include <cstdint>
#include <cstddef>
#include <mutex>

namespace chimera {

class CRC32 {
public:
    static uint32_t compute(const void* data, size_t len) {
        // FIX 3.1: std::call_once eliminates the data race on table initialization.
        // Previously: static bool init + manual check. Two threads calling compute()
        // simultaneously on first use would race on init and table[] writes —
        // technically undefined behavior even though double-init produces correct values.
        // call_once guarantees exactly-once execution with proper memory ordering.
        static std::once_flag once;
        static uint32_t table[256];

        std::call_once(once, []() {
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t c = i;
                for (size_t j = 0; j < 8; ++j)
                    c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
                table[i] = c;
            }
        });

        uint32_t crc = 0xFFFFFFFF;
        const uint8_t* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i)
            crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFF;
    }
};

}
