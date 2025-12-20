#pragma once
#include <atomic>
namespace binance {
enum class Health{GREEN,RED};
class VenueHealth {
public:
  void set(Health h){state.store(h);}
  Health get() const {return state.load();}
private:
  std::atomic<Health> state{Health::RED};
};
}
