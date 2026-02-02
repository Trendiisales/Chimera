#include "gui/GuiBroadcaster.hpp"

namespace chimera {

GuiBroadcaster::GuiBroadcaster(PositionState& ps)
    : m_positions(ps), m_ticks(0) {}

void GuiBroadcaster::onTick(uint64_t ts_ns) {
    std::lock_guard<std::mutex> g(m_lock);
    m_ticks.fetch_add(1, std::memory_order_relaxed);

    std::ostringstream ss;
    ss << "{";
    ss << "\"ts_ns\":" << ts_ns << ",";
    ss << "\"ticks\":" << m_ticks.load() << ",";
    ss << "\"equity\":" << m_positions.totalEquity();
    ss << "}";
    m_cache = ss.str();
}

std::string GuiBroadcaster::snapshotJSON() {
    std::lock_guard<std::mutex> g(m_lock);
    return m_cache;
}

}
