#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>

struct AggTrade {
    uint64_t trade_id;
    double price;
    double qty;
    uint64_t trade_time;
    bool is_buyer_maker;
};

class BinanceAggTradeWS {
public:
    using Callback = std::function<void(const AggTrade&)>;

    BinanceAggTradeWS(const std::string& symbol, Callback cb);
    ~BinanceAggTradeWS();

    void start();
    void stop();

    Callback callback_;  // Public for callback access

private:
    void run();

    std::string symbol_;
    std::thread thread_;
    std::atomic<bool> running_;
};
