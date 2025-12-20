#include "binance/BinanceSnapshot.hpp"
#include "binance/BinanceRestClient.hpp"

namespace binance {

DepthSnapshot load_snapshot() {
    BinanceRestClient client;
    return client.fetch_depth_snapshot("BTCUSDT", 1000);
}

}
