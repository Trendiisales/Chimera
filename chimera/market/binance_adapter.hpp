#ifndef BINANCE_ADAPTER_HPP
#define BINANCE_ADAPTER_HPP

#include "market_adapter.hpp"
#include <libwebsockets.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>

class BinanceAdapter : public MarketAdapter {
public:
    BinanceAdapter();
    ~BinanceAdapter() override;

    void connect() override;
    void disconnect() override;
    bool connected() const override;

    void subscribe(const std::string& symbol) override;

    void onTick(TickHandler h) override;
    void onTrade(TradeHandler h) override;
    void onDepth(DepthHandler h) override;
    void onLiquidation(LiquidationHandler h) override;

    void handleMessage(const std::string& msg);
    void handleConnect();
    void handleDisconnect();

    struct lws* getWsi() const { return wsi_; }
    struct lws_context* getContext() const { return context_; }

private:
    void wsThread();
    void buildStreamPath();
    std::string toLowerCase(const std::string& s);

    struct lws_context* context_ = nullptr;
    struct lws* wsi_ = nullptr;
    std::thread ws_thread_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    
    std::vector<std::string> symbols_;
    std::string stream_path_;
    
    TickHandler tick_handler_;
    TradeHandler trade_handler_;
    DepthHandler depth_handler_;
    LiquidationHandler liq_handler_;
    
    std::mutex handler_mtx_;
};

#endif
