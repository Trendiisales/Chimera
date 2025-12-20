#pragma once
#include <vector>
namespace binance {
struct PriceLevel { double price; double qty; };
class OrderBook {
public:
  void load_snapshot(const std::vector<PriceLevel>& b,const std::vector<PriceLevel>& a);
  bool empty() const;
  double best_bid() const;
  double best_ask() const;
private:
  std::vector<PriceLevel> bids, asks;
};
}
