#pragma once
#include <atomic>
namespace binance { enum class Health{GREEN,YELLOW,RED,DEAD}; class VenueHealth{ std::atomic<Health> h{Health::RED}; public: void set(Health v){h.store(v);} Health get() const {return h.load();} }; }
