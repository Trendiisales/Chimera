#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>

enum class EventType {
    TICK,
    DECISION,
    RISK,
    INTENT,
    VENUE_ACK,
    FILL,
    PNL
};

struct SpineEvent {
    uint64_t id;
    uint64_t ts_ns;
    EventType type;
    std::string source;
    std::string payload;
};

class EventSpine {
public:
    EventSpine();

    uint64_t publish(EventType type,
                     const std::string& source,
                     const std::string& payload,
                     uint64_t parent = 0);

    std::vector<SpineEvent> snapshot() const;

private:
    mutable std::mutex lock;
    std::vector<SpineEvent> events;
    uint64_t next_id;
};
