#pragma once

#include <cstdint>
#include <cstddef>

namespace binance {

// File header (written once)
struct BinaryLogHeader {
    uint32_t magic;        // 'B' 'L' 'O' 'G'
    uint16_t version;      // format version
    uint16_t header_size;  // sizeof(BinaryLogHeader)
    uint64_t start_ns;     // monotonic start time
    uint32_t symbol_len;   // bytes
    // followed by symbol bytes
};

// Record types
enum class RecordType : uint8_t {
    DEPTH_DELTA = 1,
    SNAPSHOT   = 2,
    HEARTBEAT  = 3
};

// Common record header
struct RecordHeader {
    uint8_t  type;         // RecordType
    uint8_t  flags;        // reserved
    uint16_t size;         // payload bytes
    uint64_t ts_ns;        // monotonic timestamp
};

// Payload for DEPTH_DELTA
struct DepthDeltaRecord {
    uint64_t U;
    uint64_t u;
    uint32_t bids_count;
    uint32_t asks_count;
    // followed by bids then asks:
    // [price(double), qty(double)] * bids_count
    // [price(double), qty(double)] * asks_count
};

} // namespace binance
