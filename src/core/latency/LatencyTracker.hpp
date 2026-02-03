#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <atomic>
#include <mutex>

#include "state/EventJournal.hpp"

namespace chimera {

struct LatencySample {
    uint64_t decision_ns = 0;
    uint64_t spine_ns = 0;
    uint64_t shadow_ns = 0;
    uint64_t venue_ns = 0;
    uint64_t ack_ns = 0;
    uint64_t gui_ns = 0;
};

class LatencyTracker {
public:
    explicit LatencyTracker(EventJournal& journal);

    void markDecision(uint64_t event_id, const std::string& key, uint64_t ts_ns);
    void markSpine(uint64_t event_id, const std::string& key, uint64_t ts_ns);
    void markShadow(uint64_t event_id, const std::string& key, uint64_t ts_ns);
    void markVenue(uint64_t event_id, const std::string& key, uint64_t ts_ns);
    void markAck(uint64_t event_id, const std::string& key, uint64_t ts_ns);
    void markGui(uint64_t event_id, const std::string& key, uint64_t ts_ns);

private:
    EventJournal& m_journal;
    std::unordered_map<std::string, LatencySample> m_samples;
    std::mutex m_lock;

    void tryEmit(uint64_t event_id, const std::string& key);
};

}
