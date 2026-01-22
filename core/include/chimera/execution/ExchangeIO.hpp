#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace chimera {

struct MarketTick {
    std::string symbol;
    double bid = 0.0;
    double ask = 0.0;
    double last = 0.0;
    double bid_size = 0.0;
    double ask_size = 0.0;
    uint64_t ts_ns = 0;
    
    // CRITICAL: Hash computed at ingestion for O(1) routing
    // A tick without symbol_hash is a malformed tick
    uint32_t symbol_hash = 0;
};

struct OrderRequest {
    std::string client_id;
    std::string symbol;
    double price = 0.0;
    double qty = 0.0;
    bool is_buy = false;
    bool post_only = false;
    bool market = false;  // For emergency flatten
};

struct OrderUpdate {
    std::string client_id;
    std::string exchange_id;
    double filled_qty = 0.0;
    double avg_price = 0.0;
    bool is_final = false;
    std::string status;
};

class IExchangeIO {
public:
    virtual ~IExchangeIO() = default;

    virtual void connect() = 0;
    virtual void disconnect() = 0;

    virtual void subscribeMarketData(
        const std::vector<std::string>& symbols
    ) = 0;

    virtual void sendOrder(const OrderRequest& req) = 0;
    virtual void cancelOrder(const std::string& client_id) = 0;

    virtual void poll() = 0;

    std::function<void(const MarketTick&)> on_tick;
    std::function<void(const OrderUpdate&)> on_order_update;
};

}
