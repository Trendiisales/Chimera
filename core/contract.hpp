#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace chimera {

struct MarketTick {
    std::string symbol;
    double bid;
    double ask;
    double bid_size;
    double ask_size;
    uint64_t ts_ns;

    // ---------------------------------------------------------------------------
    // Current net position for this symbol — injected by StrategyRunner from
    // GlobalRiskGovernor before onTick(). Positive = long, negative = short.
    // Engines use this to cap position size and avoid runaway accumulation.
    // ---------------------------------------------------------------------------
    double position{0.0};
};

struct OrderIntent {
    std::string engine_id;
    std::string symbol;
    bool is_buy;
    double price;
    double size;
};

struct FillEvent {
    std::string symbol;
    bool is_buy;
    double price;
    double size;
    uint64_t ts_ns;
};

struct ChimeraTelemetry {
    bool online;
    bool trading;
    double btc_price;
    double eth_price;
    uint64_t trades;
};

class IEngine {
public:
    virtual ~IEngine() = default;
    virtual const std::string& id() const = 0;
    virtual void onTick(const MarketTick& tick, std::vector<OrderIntent>& out) = 0;
};

class Spine {
public:
    Spine();
    void registerEngine(IEngine* engine);
    void onTick(const MarketTick& tick);
    const ChimeraTelemetry& telemetry() const;

private:
    std::vector<IEngine*> engines_;
    ChimeraTelemetry telemetry_;
    uint64_t trade_count_;
};

}
