#pragma once
#include "BinanceTypes.hpp"
#include <functional>

namespace binance {

using DeltaCallback = std::function<void(const DepthDelta&)>;

void start_ws(const DeltaCallback& cb);
void stop_ws();

}
