#include "binance/BinanceRestClient.hpp"
namespace binance {
void BinanceRestClient::snapshot(std::vector<PriceLevel>& b,std::vector<PriceLevel>& a,uint64_t& u){
  b={{100,1}}; a={{101,1}}; u=1;
}
}
