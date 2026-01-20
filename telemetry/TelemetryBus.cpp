#include "TelemetryBus.hpp"

TelemetryBus& TelemetryBus::instance() {
    static TelemetryBus bus;
    return bus;
}

void TelemetryBus::publish(const std::string& type, const nlohmann::json& payload) {
    std::lock_guard<std::mutex> lock(mtx);
    ring.push_back({type, payload});
    if (ring.size() > MAX_EVENTS) {
        ring.erase(ring.begin());
    }
}

std::vector<TelemetryEvent> TelemetryBus::snapshot() {
    std::lock_guard<std::mutex> lock(mtx);
    return ring;
}
