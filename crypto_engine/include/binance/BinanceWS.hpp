#pragma once
#include "BinanceTypes.hpp"
#include <functional>
namespace binance { using DeltaHandler=std::function<void(const DepthDelta&)>; void start_ws(const DeltaHandler&); }
