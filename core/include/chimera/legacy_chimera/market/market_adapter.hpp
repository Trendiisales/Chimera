#ifndef MARKET_ADAPTER_HPP
#define MARKET_ADAPTER_HPP

#include <string>
#include <functional>
#include <cstdint>
#include <vector>

struct Tick {
    std::string symbol;
    double bid = 0.0;
    double ask = 0.0;
    double price = 0.0;
    double spread_bps = 0.0;
    uint64_t ts_ns = 0;
};

struct TradeTick {
    std::string symbol;
    double price = 0.0;
    double qty = 0.0;
    bool is_buy = false;
    uint64_t ts_ns = 0;
};

struct DepthLevel {
    double price = 0.0;
    double qty = 0.0;
};

struct DepthUpdate {
    std::string symbol;
    std::vector<DepthLevel> bids;
    std::vector<DepthLevel> asks;
    uint64_t ts_ns = 0;
};

struct LiquidationTick {
    std::string symbol;
    double price = 0.0;
    double qty = 0.0;
    double notional = 0.0;
    bool is_long = false;
    uint64_t ts_ns = 0;
};

using TickHandler = std::function<void(const Tick&)>;
using TradeHandler = std::function<void(const TradeTick&)>;
using DepthHandler = std::function<void(const DepthUpdate&)>;
using LiquidationHandler = std::function<void(const LiquidationTick&)>;

class MarketAdapter {
public:
    virtual ~MarketAdapter() = default;
    
    virtual void connect() = 0;
    virtual void disconnect() = 0;
    virtual bool connected() const = 0;
    
    virtual void subscribe(const std::string& symbol) = 0;
    
    virtual void onTick(TickHandler h) = 0;
    virtual void onTrade(TradeHandler h) = 0;
    virtual void onDepth(DepthHandler h) = 0;
    virtual void onLiquidation(LiquidationHandler h) = 0;
};

#endif
