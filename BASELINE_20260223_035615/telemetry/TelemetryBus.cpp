#include "TelemetryBus.hpp"

namespace chimera {

void TelemetryBus::subscribe(TelemetrySink* sink) {
    std::lock_guard<std::mutex> g(m_lock);
    m_sinks.push_back(sink);
}

void TelemetryBus::publish(const TelemetryEvent& ev) {
    std::lock_guard<std::mutex> g(m_lock);
    for (TelemetrySink* s : m_sinks) {
        s->publish(ev);
    }
}

}
