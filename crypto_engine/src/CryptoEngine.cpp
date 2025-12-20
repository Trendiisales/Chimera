#include "binance/BinanceDepthFeed.hpp"
#include "binance/BinaryLogWriter.hpp"
#include <iostream>
using namespace binance;
void run_crypto_engine(){
  BinanceRestClient r; OrderBook b; DeltaGate g; VenueHealth h; BinaryLogWriter log("test.blog");
  BinanceDepthFeed feed(r,b,g,h,log); feed.start();
  std::cout<<"ENGINE OK\n";
}
