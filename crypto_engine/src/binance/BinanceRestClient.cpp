#include "binance/BinanceRestClient.hpp"
#include <iostream>

namespace binance {

DepthSnapshot BinanceRestClient::fetch_depth_snapshot(const std::string& symbol, int limit) {
    /*
      REAL IMPLEMENTATION (TOMORROW):
        - HTTP GET to /api/v3/depth
        - parse JSON
        - fill bids/asks
        - read lastUpdateId

      TONIGHT:
        - simulate authoritative snapshot
        - prove control-flow correctness
    */

    static uint64_t last_id = 5000;

    DepthSnapshot snap;
    snap.lastUpdateId = last_id;

    std::cout << "[REST] /depth snapshot for " << symbol
              << " lastUpdateId=" << snap.lastUpdateId
              << " limit=" << limit << std::endl;

    last_id += 250;

    return snap;
}

}
