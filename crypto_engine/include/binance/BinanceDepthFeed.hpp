#pragma once
#include "OrderBook.hpp"
#include "DeltaGate.hpp"
#include "VenueHealth.hpp"
#include "BinaryLogWriter.hpp"
#include "BinanceRestClient.hpp"
namespace binance {
class BinanceDepthFeed {
public:
  BinanceDepthFeed(BinanceRestClient&,OrderBook&,DeltaGate&,VenueHealth&,BinaryLogWriter&);
  void start();
private:
  BinanceRestClient& r; OrderBook& b; DeltaGate& g; VenueHealth& h; BinaryLogWriter& log;
};
}
