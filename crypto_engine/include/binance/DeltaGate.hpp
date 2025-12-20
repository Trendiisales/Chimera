#pragma once
#include <cstdint>
namespace binance {
class DeltaGate {
public:
  void reset(uint64_t u);
  bool accept(uint64_t u);
private:
  uint64_t last=0;
  bool init=false;
};
}
