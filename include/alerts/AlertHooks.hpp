#pragma once

namespace Chimera {

// Call from state machines / arbiter
void alert_fix_halted();
void alert_binance_blind();
void alert_exec_throttled();
void alert_divergence();

}
