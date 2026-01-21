#include "EventBus.hpp"
#include <cstring>

namespace chimera_lab {

static uint32_t crc_table[256];
static bool crc_init = false;

static void init_crc() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
        crc_table[i] = c;
    }
    crc_init = true;
}

EventBus::EventBus(const std::string& file) {
    if (!crc_init) init_crc();
    out.open(file, std::ios::binary | std::ios::app);
}

EventBus::~EventBus() {
    out.close();
}

uint32_t EventBus::crc32(const uint8_t* data, size_t len) {
    uint32_t c = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++)
        c = crc_table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFF;
}

void EventBus::writeFrame(const EventHeader& hdr, const void* payload) {
    std::lock_guard<std::mutex> lock(mtx);
    out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    if (hdr.payload_size > 0) {
        out.write(reinterpret_cast<const char*>(payload), hdr.payload_size);
    }
    out.flush();
}

void EventBus::logSignal(uint64_t id, uint64_t ts_ex, uint64_t ts_local,
                         uint32_t sym, uint8_t venue, uint8_t engine,
                         const SignalVector& vec) {
    EventHeader hdr{};
    hdr.event_id = id;
    hdr.ts_exchange = ts_ex;
    hdr.ts_local = ts_local;
    hdr.symbol_hash = sym;
    hdr.venue = venue;
    hdr.engine_id = engine;
    hdr.type = EventType::SIGNAL;
    hdr.payload_size = sizeof(SignalVector);
    hdr.crc32 = crc32(reinterpret_cast<const uint8_t*>(&vec), sizeof(vec));
    writeFrame(hdr, &vec);
}

void EventBus::logDecision(uint64_t id, uint64_t ts_ex, uint64_t ts_local,
                           uint32_t sym, uint8_t venue, uint8_t engine,
                           const DecisionPayload& dec) {
    EventHeader hdr{};
    hdr.event_id = id;
    hdr.ts_exchange = ts_ex;
    hdr.ts_local = ts_local;
    hdr.symbol_hash = sym;
    hdr.venue = venue;
    hdr.engine_id = engine;
    hdr.type = EventType::DECISION;
    hdr.payload_size = sizeof(DecisionPayload);
    hdr.crc32 = crc32(reinterpret_cast<const uint8_t*>(&dec), sizeof(dec));
    writeFrame(hdr, &dec);
}

void EventBus::logFill(uint64_t id, uint64_t ts_ex, uint64_t ts_local,
                       uint32_t sym, uint8_t venue, uint8_t engine,
                       const FillPayload& fill) {
    EventHeader hdr{};
    hdr.event_id = id;
    hdr.ts_exchange = ts_ex;
    hdr.ts_local = ts_local;
    hdr.symbol_hash = sym;
    hdr.venue = venue;
    hdr.engine_id = engine;
    hdr.type = EventType::FILL;
    hdr.payload_size = sizeof(FillPayload);
    hdr.crc32 = crc32(reinterpret_cast<const uint8_t*>(&fill), sizeof(fill));
    writeFrame(hdr, &fill);
}

} // namespace chimera_lab
