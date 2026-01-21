#pragma once

#include <string>
#include <unordered_map>

#include "chimera/execution/MarketBus.hpp"
#include "chimera/execution/OrderManager.hpp"
#include "chimera/survival/EdgeSurvivalFilter.hpp"

namespace chimera {

struct QuoteConfig {
    double min_spread_bps = 2.0;
    double quote_bps = 0.5;
    double order_size = 0.01;
    double max_volatility = 0.2;
};

struct ActiveQuote {
    std::string bid_id;
    std::string ask_id;
    double bid_price = 0.0;
    double ask_price = 0.0;
};

class MakerRebateEngine {
public:
    MakerRebateEngine(
        MarketBus& market,
        EdgeSurvivalFilter& survival,
        OrderManager& orders
    );

    void setConfig(
        const std::string& symbol,
        const QuoteConfig& cfg
    );

    void onTick(
        const std::string& symbol,
        uint64_t ts_ns
    );

    void cancelAll();

private:
    void placeQuotes(
        const std::string& symbol,
        const QuoteConfig& cfg,
        double mid
    );

    void cancelQuotes(
        const std::string& symbol
    );

private:
    MarketBus& market_bus;
    EdgeSurvivalFilter& survival_filter;
    OrderManager& order_manager;

    std::unordered_map<std::string, QuoteConfig> configs;
    std::unordered_map<std::string, ActiveQuote> live_quotes;
};

}
