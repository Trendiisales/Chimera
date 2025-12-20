#include "binance/DeltaGate.hpp"
namespace binance {
void DeltaGate::reset(uint64_t u){last=u;init=true;}
bool DeltaGate::accept(uint64_t u){if(!init||u<=last)return false;last=u;return true;}
}
