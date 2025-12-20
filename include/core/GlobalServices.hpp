#pragma once
#include "logging/BinaryLog.hpp"
#include "arbiter/VenueHealth.hpp"

namespace Chimera {

enum VenueId : uint16_t {
    VENUE_BINANCE = 1,
    VENUE_FIX = 2
};

struct GlobalServices {
    BinaryLogWriter* logger;
    VenueHealth* binance_health;
    VenueHealth* fix_health;
};

extern GlobalServices g_services;

}
