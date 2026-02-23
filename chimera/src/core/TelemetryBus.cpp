#include "TelemetryBus.hpp"

TelemetryBus& TelemetryBus::instance() {
    static TelemetryBus bus;
    return bus;
}

void TelemetryBus::tick() {
    std::lock_guard<std::mutex> l(mtx_);
    snap_.ticks++;
}

void TelemetryBus::order() {
    std::lock_guard<std::mutex> l(mtx_);
    snap_.orders++;
}

void TelemetryBus::fill() {
    std::lock_guard<std::mutex> l(mtx_);
    snap_.fills++;
}

void TelemetryBus::set_pnl(double v) {
    std::lock_guard<std::mutex> l(mtx_);
    snap_.pnl = v;
}

void TelemetryBus::set_latency(int ms) {
    std::lock_guard<std::mutex> l(mtx_);
    snap_.latency_ms = ms;
}

void TelemetryBus::set_regime(const std::string& r) {
    std::lock_guard<std::mutex> l(mtx_);
    snap_.regime = r;
}

TelemetrySnapshot TelemetryBus::snapshot() {
    std::lock_guard<std::mutex> l(mtx_);
    return snap_;
}
