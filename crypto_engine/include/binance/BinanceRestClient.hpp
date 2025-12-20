#pragma once
#include "BinanceTypes.hpp"
#include <string>

namespace binance {

/*
 REST SNAPSHOT CONTRACT (Binance):
   GET /api/v3/depth?symbol=BTCUSDT&limit=1000

 Response fields we care about:
   lastUpdateId (authoritative)
   bids: [[price, qty], ...]
   asks: [[price, qty], ...]

 This interface is CONTROL-PLANE ONLY.
*/
class BinanceRestClient {
public:
    DepthSnapshot fetch_depth_snapshot(const std::string& symbol, int limit);
};

}
