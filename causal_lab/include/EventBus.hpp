#pragma once
#include "EventTypes.hpp"
#include <fstream>
#include <mutex>
#include <string>

namespace chimera_lab {

class EventBus {
public:
    explicit EventBus(const std::string& file);
    ~EventBus();

    void logSignal(uint64_t id,
                   uint64_t ts_ex,
                   uint64_t ts_local,
                   uint32_t sym,
                   uint8_t venue,
                   uint8_t engine,
                   const SignalVector& vec);

    void logDecision(uint64_t id,
                     uint64_t ts_ex,
                     uint64_t ts_local,
                     uint32_t sym,
                     uint8_t venue,
                     uint8_t engine,
                     const DecisionPayload& dec);

    void logFill(uint64_t id,
                 uint64_t ts_ex,
                 uint64_t ts_local,
                 uint32_t sym,
                 uint8_t venue,
                 uint8_t engine,
                 const FillPayload& fill);

private:
    std::ofstream out;
    std::mutex mtx;

    uint32_t crc32(const uint8_t* data, size_t len);
    void writeFrame(const EventHeader& hdr, const void* payload);
};

} // namespace chimera_lab
