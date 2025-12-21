#pragma once
#include <string>
#include <cstdint>

struct Fill {
    std::string symbol;
    double qty;
    double price;
    double fee;
    int64_t ts_ns;
};
