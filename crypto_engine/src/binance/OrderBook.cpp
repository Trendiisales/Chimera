#include "binance/OrderBook.hpp"
namespace binance {
void OrderBook::load_snapshot(const std::vector<PriceLevel>& b,const std::vector<PriceLevel>& a){bids=b;asks=a;}
bool OrderBook::empty() const {return bids.empty()||asks.empty();}
double OrderBook::best_bid() const {return bids.front().price;}
double OrderBook::best_ask() const {return asks.front().price;}
}
