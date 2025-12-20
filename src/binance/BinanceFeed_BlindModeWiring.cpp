#include "binance/BinanceBlindMode.hpp"
#include "core/GlobalServices.hpp"
#include <chrono>

using namespace Chimera;

static BlindModeState g_blind_state;
static BinanceBlindMode g_blind(g_blind_state);

static inline uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void on_binance_depth_update_blind(uint64_t depth_ts_ns) {
    g_blind.on_depth_update(depth_ts_ns);

    if (g_services.logger) {
        g_services.logger->write(
            &depth_ts_ns,
            sizeof(depth_ts_ns),
            LogRecordType::SYSTEM,
            VENUE_BINANCE
        );
    }
}

bool binance_should_trade_blind() {
    return g_blind.should_blind(now_ns());
}

double binance_price_adjust(double px, bool is_bid) {
    return g_blind.widen_price(px, is_bid);
}

double binance_qty_adjust(double qty) {
    return g_blind.cap_qty(qty);
}
