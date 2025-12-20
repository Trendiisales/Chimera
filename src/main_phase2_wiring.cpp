#include "core/GlobalServices.hpp"
#include "logging/BinaryLog.hpp"
#include "arbiter/VenueHealth.hpp"

using namespace Chimera;

void init_phase2_services() {
    static BinaryLogWriter logger("chimera.binlog");
    static VenueHealth binance_health;
    static VenueHealth fix_health;

    g_services.logger = &logger;
    g_services.binance_health = &binance_health;
    g_services.fix_health = &fix_health;
}
