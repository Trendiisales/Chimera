#include "latency/LatencyTracker.hpp"
#include <sstream>

namespace chimera {

LatencyTracker::LatencyTracker(EventJournal& journal)
    : m_journal(journal) {}

void LatencyTracker::markDecision(uint64_t, const std::string& key, uint64_t ts_ns) {
    std::lock_guard<std::mutex> g(m_lock);
    m_samples[key].decision_ns = ts_ns;
}

void LatencyTracker::markSpine(uint64_t event_id, const std::string& key, uint64_t ts_ns) {
    std::lock_guard<std::mutex> g(m_lock);
    m_samples[key].spine_ns = ts_ns;
    tryEmit(event_id, key);
}

void LatencyTracker::markShadow(uint64_t event_id, const std::string& key, uint64_t ts_ns) {
    std::lock_guard<std::mutex> g(m_lock);
    m_samples[key].shadow_ns = ts_ns;
    tryEmit(event_id, key);
}

void LatencyTracker::markVenue(uint64_t event_id, const std::string& key, uint64_t ts_ns) {
    std::lock_guard<std::mutex> g(m_lock);
    m_samples[key].venue_ns = ts_ns;
    tryEmit(event_id, key);
}

void LatencyTracker::markAck(uint64_t event_id, const std::string& key, uint64_t ts_ns) {
    std::lock_guard<std::mutex> g(m_lock);
    m_samples[key].ack_ns = ts_ns;
    tryEmit(event_id, key);
}

void LatencyTracker::markGui(uint64_t event_id, const std::string& key, uint64_t ts_ns) {
    std::lock_guard<std::mutex> g(m_lock);
    m_samples[key].gui_ns = ts_ns;
    tryEmit(event_id, key);
}

void LatencyTracker::tryEmit(uint64_t event_id, const std::string& key) {
    auto it = m_samples.find(key);
    if (it == m_samples.end()) return;

    const LatencySample& s = it->second;

    if (s.decision_ns == 0 ||
        s.spine_ns == 0 ||
        s.shadow_ns == 0 ||
        s.venue_ns == 0 ||
        s.ack_ns == 0 ||
        s.gui_ns == 0) {
        return;
    }

    uint64_t d_to_s = s.spine_ns - s.decision_ns;
    uint64_t s_to_sh = s.shadow_ns - s.spine_ns;
    uint64_t sh_to_v = s.venue_ns - s.shadow_ns;
    uint64_t v_to_a = s.ack_ns - s.venue_ns;
    uint64_t a_to_g = s.gui_ns - s.ack_ns;

    std::ostringstream payload;
    payload << "{"
            << "\"key\":\"" << key << "\","
            << "\"decision_to_spine_ns\":" << d_to_s << ","
            << "\"spine_to_shadow_ns\":" << s_to_sh << ","
            << "\"shadow_to_venue_ns\":" << sh_to_v << ","
            << "\"venue_to_ack_ns\":" << v_to_a << ","
            << "\"ack_to_gui_ns\":" << a_to_g
            << "}";

    m_journal.write("LATENCY_SAMPLE", payload.str(), event_id);
    m_samples.erase(it);
}

}
