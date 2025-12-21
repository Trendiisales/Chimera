#include "binance/BinanceDepthFeed.hpp"

namespace binance {

BinanceDepthFeed::BinanceDepthFeed(
    BinanceRestClient& rest,
    OrderBook& book,
    DeltaGate& gate,
    VenueHealth& health,
    BinaryLogWriter& log_writer
)
: r(rest)
, b(book)
, g(gate)
, h(health)
, log(log_writer)
{
}

void BinanceDepthFeed::start() {
    // Depth feed lifecycle is controlled externally (Supervisor)
    // No local state changes required here
}

} // namespace binance
