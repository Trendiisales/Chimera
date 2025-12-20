#include "alerts/AlertKillSwitch.hpp"
#include "alerts/AlertBus.hpp"

using namespace Chimera;

bool alert_kill_active() {
    return alert_bus().critical_active();
}
