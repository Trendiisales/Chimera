#include "binance/BinanceDepthFeed.hpp"
namespace binance {
BinanceDepthFeed::BinanceDepthFeed(BinanceRestClient& r_,OrderBook& b_,DeltaGate& g_,VenueHealth& h_,BinaryLogWriter& l_)
:r(r_),b(b_),g(g_),h(h_),log(l_){}
void BinanceDepthFeed::start(){
  std::vector<PriceLevel> bids,asks; uint64_t u=0;
  r.snapshot(bids,asks,u); b.load_snapshot(bids,asks); g.reset(u); h.set(Health::GREEN);
}
}
