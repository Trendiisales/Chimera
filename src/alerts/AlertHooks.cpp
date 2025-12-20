#include "alerts/AlertHooks.hpp"
#include "alerts/AlertBus.hpp"

using namespace Chimera;

void alert_fix_halted() {
    alert_bus().emit(AlertCode::FIX_HALTED, AlertLevel::CRITICAL);
}

void alert_binance_blind() {
    alert_bus().emit(AlertCode::BINANCE_BLIND, AlertLevel::WARN);
}

void alert_exec_throttled() {
    alert_bus().emit(AlertCode::EXEC_THROTTLED, AlertLevel::WARN);
}

void alert_divergence() {
    alert_bus().emit(AlertCode::DIVERGENCE, AlertLevel::CRITICAL);
}
