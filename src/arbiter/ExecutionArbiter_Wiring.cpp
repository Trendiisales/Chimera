#include "arbiter/ExecutionArbiter.hpp"
#include "binance/BinanceBlindMode.hpp"
#include "fix/FixDegradedState.hpp"

using namespace Chimera;

static ExecutionArbiter g_arbiter;

ExecutionArbiter& execution_arbiter() {
    return g_arbiter;
}

void arbiter_update_from_binance(bool blind_active) {
    g_arbiter.update_binance(blind_active);
}

void arbiter_update_from_fix(const FixDegradedState& fix_state) {
    g_arbiter.update_fix(
        fix_state.allow_new_orders(),
        fix_state.size_multiplier()
    );
}
