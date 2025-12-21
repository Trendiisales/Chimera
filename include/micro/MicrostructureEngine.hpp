#pragma once

#include <atomic>
#include <string>
#include <unordered_map>

namespace binance {
class OrderBook;
}

struct MicroSnapshot {
    std::atomic<double> mid{0.0};
    std::atomic<double> spread{0.0};
    std::atomic<double> spread_bps{0.0};
};

class MicrostructureEngine {
public:
    explicit MicrostructureEngine(
        const std::unordered_map<std::string, binance::OrderBook*>& books
    );

    void update();

    double mid(const std::string& symbol) const;
    double spread_bps(const std::string& symbol) const;

private:
    std::unordered_map<std::string, binance::OrderBook*> books_;
    std::unordered_map<std::string, MicroSnapshot> snaps_;
};
