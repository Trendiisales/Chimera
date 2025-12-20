#include "arbiter/ExecutionArbiter.hpp"

using namespace Chimera;

ExecutionArbiter::ExecutionArbiter() {
    binance_ok_.store(true, std::memory_order_relaxed);
    fix_ok_.store(true, std::memory_order_relaxed);
    fix_size_mult_.store(1.0, std::memory_order_relaxed);
}

void ExecutionArbiter::update_binance(bool blind_active) {
    binance_ok_.store(!blind_active, std::memory_order_relaxed);
}

void ExecutionArbiter::update_fix(bool allow_orders, double size_mult) {
    fix_ok_.store(allow_orders, std::memory_order_relaxed);
    fix_size_mult_.store(size_mult, std::memory_order_relaxed);
}

bool ExecutionArbiter::allow_execution() const {
    bool b = binance_ok_.load(std::memory_order_relaxed);
    bool f = fix_ok_.load(std::memory_order_relaxed);
    return b && f;
}

double ExecutionArbiter::size_multiplier() const {
    return fix_size_mult_.load(std::memory_order_relaxed);
}
