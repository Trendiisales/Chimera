#include "state/EventJournal.hpp"
#include <chrono>

namespace chimera {

static uint64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

EventJournal::EventJournal(const std::string& path)
    : m_event_id(1) {
    m_bin.open(path + ".bin", std::ios::binary | std::ios::app);
    m_json.open(path + ".jsonl", std::ios::app);
}

EventJournal::~EventJournal() {
    m_bin.flush();
    m_json.flush();
}

uint64_t EventJournal::nextEventId() {
    return m_event_id.fetch_add(1, std::memory_order_relaxed);
}

void EventJournal::write(const std::string& type,
                         const std::string& payload,
                         uint64_t causal_id) {
    uint64_t id = nextEventId();
    uint64_t ts = nowNs();

    m_bin.write(reinterpret_cast<char*>(&id), sizeof(id));
    m_bin.write(reinterpret_cast<char*>(&ts), sizeof(ts));

    m_json << "{"
           << "\"id\":" << id << ","
           << "\"ts_ns\":" << ts << ","
           << "\"type\":\"" << type << "\","
           << "\"causal\":" << causal_id << ","
           << "\"payload\":" << payload
           << "}\n";
}

}
