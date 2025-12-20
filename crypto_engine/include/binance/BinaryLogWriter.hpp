#pragma once

#include "binance/BinaryLog.hpp"

#include <string>
#include <cstddef>
#include <cstdint>

namespace binance {

class BinaryLogWriter {
public:
    BinaryLogWriter(
        const std::string& path,
        const std::string& symbol,
        size_t map_size_bytes = 256 * 1024 * 1024
    );

    ~BinaryLogWriter();

    void write_snapshot(const void* data, size_t bytes, uint64_t ts_ns);
    void write_depth_delta(
        uint64_t U, uint64_t u,
        const void* payload, size_t bytes,
        uint64_t ts_ns
    );

    void flush();

private:
    int fd{-1};
    uint8_t* map{nullptr};
    size_t map_size{0};
    size_t write_off{0};

    void ensure_space(size_t need);
    void write_bytes(const void* src, size_t bytes);
};

} // namespace binance
