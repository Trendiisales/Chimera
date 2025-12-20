#pragma once
#include "BinanceTypes.hpp"
#include <string>
#include <optional>

namespace binance {

/*
 simdjson-based parser for Binance depth messages.
 Handles combined streams:
 { "stream":"btcusdt@depth@100ms", "data":{...} }
*/
class BinanceDepthParser {
public:
    static std::optional<DepthDelta> parse(const std::string& raw);
};

}
