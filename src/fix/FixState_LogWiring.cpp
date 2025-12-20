#include "fix/FixDegradedState.hpp"
#include "core/GlobalServices.hpp"

using namespace Chimera;

static FixDegradedState g_fix_state;

FixDegradedState& fix_state_machine() {
    return g_fix_state;
}

void log_fix_state_change(FixState s) {
    if (!g_services.logger) return;

    uint8_t v = static_cast<uint8_t>(s);
    g_services.logger->write(
        &v,
        sizeof(v),
        LogRecordType::SYSTEM,
        VENUE_FIX
    );
}
