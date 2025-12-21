#pragma once

#include "binance/OrderBook.hpp"

#include <string>
#include <unordered_map>

class MicrostructureEngine {
public:
    explicit MicrostructureEngine(
        const std::unordered_map<std::string, binance::OrderBook*>& books
    );

    void update();

    double mid(const std::string& symbol) const;
    double spread_bps(const std::string& symbol) const;

    const std::unordered_map<std::string, binance::OrderBook*>& symbols() const {
        return books_;
    }

private:
    const std::unordered_map<std::string, binance::OrderBook*>& books_;

    std::unordered_map<std::string, double> mid_;
    std::unordered_map<std::string, double> spread_;
    std::unordered_map<std::string, double> spread_bps_;
    std::unordered_map<std::string, double> imbalance_;
};
