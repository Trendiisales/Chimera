#include "alerts/AlertBus.hpp"
#include <chrono>
#include <iostream>

using namespace Chimera;

static AlertBus g_bus;

AlertBus::AlertBus() : critical_(false) {}

AlertBus& Chimera::alert_bus() {
    return g_bus;
}

void AlertBus::emit(AlertCode code, AlertLevel level) {
    uint64_t ts_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();

    if (level == AlertLevel::CRITICAL) {
        critical_.store(true, std::memory_order_relaxed);
    }

    std::cerr
        << "[ALERT] ts=" << ts_ns
        << " code=" << static_cast<uint16_t>(code)
        << " level=" << static_cast<uint8_t>(level)
        << "\n";
}

bool AlertBus::critical_active() const {
    return critical_.load(std::memory_order_relaxed);
}
