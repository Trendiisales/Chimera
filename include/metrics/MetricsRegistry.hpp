#pragma once
#include "metrics/MetricsTypes.hpp"
#include <atomic>

namespace Chimera {

class MetricsRegistry {
public:
    MetricsRegistry();

    void inc_binance_tick();
    void inc_fix_exec();
    void inc_exec_allowed();
    void inc_exec_blocked();
    void inc_divergence();
    void inc_alert_critical();

    MetricsSnapshot snapshot() const;

private:
    std::atomic<uint64_t> binance_ticks_;
    std::atomic<uint64_t> fix_execs_;
    std::atomic<uint64_t> exec_allowed_;
    std::atomic<uint64_t> exec_blocked_;
    std::atomic<uint64_t> divergences_;
    std::atomic<uint64_t> alerts_critical_;
};

MetricsRegistry& metrics();

}
