#pragma once
#include <string>

struct ExchangeInfo {
    double tick_size = 0.01;
    double lot_size = 0.001;
};

class ExchangeInfoCache {
public:
    ExchangeInfoCache() = default;

    ExchangeInfo get(const std::string&) {
        return ExchangeInfo{};
    }
};
