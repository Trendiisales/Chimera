#include "metrics/MetricsRegistry.hpp"
#include <chrono>

using namespace Chimera;

static MetricsRegistry g_metrics;

MetricsRegistry::MetricsRegistry()
    : binance_ticks_(0),
      fix_execs_(0),
      exec_allowed_(0),
      exec_blocked_(0),
      divergences_(0),
      alerts_critical_(0) {}

MetricsRegistry& Chimera::metrics() {
    return g_metrics;
}

void MetricsRegistry::inc_binance_tick() {
    binance_ticks_.fetch_add(1, std::memory_order_relaxed);
}

void MetricsRegistry::inc_fix_exec() {
    fix_execs_.fetch_add(1, std::memory_order_relaxed);
}

void MetricsRegistry::inc_exec_allowed() {
    exec_allowed_.fetch_add(1, std::memory_order_relaxed);
}

void MetricsRegistry::inc_exec_blocked() {
    exec_blocked_.fetch_add(1, std::memory_order_relaxed);
}

void MetricsRegistry::inc_divergence() {
    divergences_.fetch_add(1, std::memory_order_relaxed);
}

void MetricsRegistry::inc_alert_critical() {
    alerts_critical_.fetch_add(1, std::memory_order_relaxed);
}

MetricsSnapshot MetricsRegistry::snapshot() const {
    MetricsSnapshot s{};
    s.ts_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();

    s.binance_ticks   = binance_ticks_.load(std::memory_order_relaxed);
    s.fix_execs       = fix_execs_.load(std::memory_order_relaxed);
    s.exec_allowed    = exec_allowed_.load(std::memory_order_relaxed);
    s.exec_blocked    = exec_blocked_.load(std::memory_order_relaxed);
    s.divergences     = divergences_.load(std::memory_order_relaxed);
    s.alerts_critical = alerts_critical_.load(std::memory_order_relaxed);
    return s;
}
