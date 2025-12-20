#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace binance {

struct PriceLevel {
    double price;
    double qty;
};

struct DepthSnapshot {
    uint64_t lastUpdateId;
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
};

struct DepthDelta {
    std::string symbol;   // <-- NEW
    uint64_t U;
    uint64_t u;
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
};

}
