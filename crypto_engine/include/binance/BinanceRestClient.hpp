#pragma once
#include "OrderBook.hpp"
namespace binance {
class BinanceRestClient {
public:
  void snapshot(std::vector<PriceLevel>& b,std::vector<PriceLevel>& a,uint64_t& u);
};
}
