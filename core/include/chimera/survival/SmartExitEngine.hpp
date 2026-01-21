#pragma once

#include <string>
#include <unordered_map>

#include "chimera/execution/PositionBook.hpp"
#include "chimera/execution/OrderManager.hpp"
#include "chimera/execution/MarketBus.hpp"

namespace chimera {

struct ExitProfile {
    double take_profit_bps = 12.0;
    double stop_loss_bps = 8.0;
    double time_decay_sec = 30.0;
};

struct LiveTrade {
    std::string symbol;
    bool is_long = true;
    double entry_price = 0.0;
    double qty = 0.0;
    uint64_t open_ts = 0;
};

class SmartExitEngine {
public:
    SmartExitEngine(
        PositionBook& book,
        OrderManager& orders,
        MarketBus& market
    );

    void onFill(
        const std::string& symbol,
        bool is_buy,
        double qty,
        double price,
        uint64_t ts_ns
    );

    void poll(uint64_t now_ns);

    void setExitProfile(
        const std::string& symbol,
        const ExitProfile& prof
    );

private:
    void evaluateExit(
        const LiveTrade& trade,
        const ExitProfile& prof,
        uint64_t now_ns
    );

private:
    PositionBook& position_book;
    OrderManager& order_manager;
    MarketBus& market_bus;

    std::unordered_map<std::string, LiveTrade> live_trades;
    std::unordered_map<std::string, ExitProfile> profiles;
};

}
