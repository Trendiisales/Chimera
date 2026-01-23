#include "chimera/binance_adapter.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <cstring>

using json = nlohmann::json;

static BinanceAdapter* g_adapter = nullptr;

static int ws_callback(
    struct lws* wsi,
    enum lws_callback_reasons reason,
    void* user,
    void* in,
    size_t len
) {
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            if (g_adapter) g_adapter->handleConnect();
            break;
            
        case LWS_CALLBACK_CLIENT_RECEIVE:
            if (g_adapter && in && len > 0) {
                std::string msg(static_cast<char*>(in), len);
                g_adapter->handleMessage(msg);
            }
            break;
            
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        case LWS_CALLBACK_CLIENT_CLOSED:
            if (g_adapter) g_adapter->handleDisconnect();
            break;
            
        default:
            break;
    }
    return 0;
}

static const struct lws_protocols protocols[] = {
    {"binance", ws_callback, 0, 65536},
    {nullptr, nullptr, 0, 0}
};

BinanceAdapter::BinanceAdapter() {
    g_adapter = this;
}

BinanceAdapter::~BinanceAdapter() {
    disconnect();
    g_adapter = nullptr;
}

void BinanceAdapter::connect() {
    if (running_.load()) return;
    
    buildStreamPath();
    
    running_.store(true);
    ws_thread_ = std::thread(&BinanceAdapter::wsThread, this);
}

void BinanceAdapter::disconnect() {
    running_.store(false);
    
    if (ws_thread_.joinable()) {
        ws_thread_.join();
    }
    
    if (context_) {
        lws_context_destroy(context_);
        context_ = nullptr;
    }
    
    wsi_ = nullptr;
    connected_.store(false);
}

bool BinanceAdapter::connected() const {
    return connected_.load();
}

void BinanceAdapter::subscribe(const std::string& symbol) {
    symbols_.push_back(symbol);
}

void BinanceAdapter::onTick(TickHandler h) {
    std::lock_guard<std::mutex> lock(handler_mtx_);
    tick_handler_ = std::move(h);
}

void BinanceAdapter::onTrade(TradeHandler h) {
    std::lock_guard<std::mutex> lock(handler_mtx_);
    trade_handler_ = std::move(h);
}

void BinanceAdapter::onDepth(DepthHandler h) {
    std::lock_guard<std::mutex> lock(handler_mtx_);
    depth_handler_ = std::move(h);
}

void BinanceAdapter::onLiquidation(LiquidationHandler h) {
    std::lock_guard<std::mutex> lock(handler_mtx_);
    liq_handler_ = std::move(h);
}

std::string BinanceAdapter::toLowerCase(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

void BinanceAdapter::buildStreamPath() {
    stream_path_ = "/stream?streams=";
    
    bool first = true;
    for (const auto& sym : symbols_) {
        std::string lower = toLowerCase(sym);
        
        if (!first) stream_path_ += "/";
        first = false;
        
        stream_path_ += lower + "@bookTicker";
        stream_path_ += "/" + lower + "@aggTrade";
        stream_path_ += "/" + lower + "@depth@100ms";
        stream_path_ += "/" + lower + "@forceOrder";
    }
}

void BinanceAdapter::wsThread() {
    struct lws_context_creation_info ctx_info = {};
    ctx_info.port = CONTEXT_PORT_NO_LISTEN;
    ctx_info.protocols = protocols;
    ctx_info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    ctx_info.timeout_secs = 30;
    
    context_ = lws_create_context(&ctx_info);
    if (!context_) {
        std::cerr << "[BINANCE] Failed to create context\n";
        return;
    }
    
    struct lws_client_connect_info conn_info = {};
    conn_info.context = context_;
    conn_info.address = "fstream.binance.com";
    conn_info.port = 443;
    conn_info.path = stream_path_.c_str();
    conn_info.host = conn_info.address;
    conn_info.origin = conn_info.address;
    conn_info.protocol = protocols[0].name;
    conn_info.ssl_connection = LCCSCF_USE_SSL;
    
    std::cout << "[BINANCE] Connecting to wss://fstream.binance.com" << stream_path_ << "\n";
    
    wsi_ = lws_client_connect_via_info(&conn_info);
    if (!wsi_) {
        std::cerr << "[BINANCE] Connection failed\n";
        return;
    }
    
    while (running_.load()) {
        lws_service(context_, 50);
    }
}

void BinanceAdapter::handleConnect() {
    connected_.store(true);
    std::cout << "[BINANCE] WebSocket connected\n";
}

void BinanceAdapter::handleDisconnect() {
    connected_.store(false);
    std::cout << "[BINANCE] WebSocket disconnected\n";
}

void BinanceAdapter::handleMessage(const std::string& msg) {
    try {
        auto j = json::parse(msg);
        
        if (!j.contains("stream") || !j.contains("data"))
            return;
        
        std::string stream = j["stream"];
        auto& d = j["data"];
        
        auto now = std::chrono::high_resolution_clock::now();
        uint64_t ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()
        ).count();
        
        if (stream.find("@bookTicker") != std::string::npos) {
            Tick t;
            t.symbol = d["s"];
            t.bid = std::stod(d["b"].get<std::string>());
            t.ask = std::stod(d["a"].get<std::string>());
            t.price = (t.bid + t.ask) / 2.0;
            t.ts_ns = ts_ns;
            
            if (t.price > 0.0) {
                t.spread_bps = (t.ask - t.bid) / t.price * 10000.0;
            }
            
            std::lock_guard<std::mutex> lock(handler_mtx_);
            if (tick_handler_) tick_handler_(t);
        }
        else if (stream.find("@aggTrade") != std::string::npos) {
            TradeTick t;
            t.symbol = d["s"];
            t.price = std::stod(d["p"].get<std::string>());
            t.qty = std::stod(d["q"].get<std::string>());
            t.is_buy = !d["m"].get<bool>();
            t.ts_ns = ts_ns;
            
            std::lock_guard<std::mutex> lock(handler_mtx_);
            if (trade_handler_) trade_handler_(t);
        }
        else if (stream.find("@depth") != std::string::npos) {
            DepthUpdate du;
            
            size_t at_pos = stream.find('@');
            if (at_pos != std::string::npos) {
                du.symbol = stream.substr(0, at_pos);
                std::transform(du.symbol.begin(), du.symbol.end(), 
                              du.symbol.begin(), ::toupper);
            }
            
            du.ts_ns = ts_ns;
            
            if (d.contains("b")) {
                for (auto& b : d["b"]) {
                    DepthLevel lvl;
                    lvl.price = std::stod(b[0].get<std::string>());
                    lvl.qty = std::stod(b[1].get<std::string>());
                    du.bids.push_back(lvl);
                }
            }
            
            if (d.contains("a")) {
                for (auto& a : d["a"]) {
                    DepthLevel lvl;
                    lvl.price = std::stod(a[0].get<std::string>());
                    lvl.qty = std::stod(a[1].get<std::string>());
                    du.asks.push_back(lvl);
                }
            }
            
            std::lock_guard<std::mutex> lock(handler_mtx_);
            if (depth_handler_) depth_handler_(du);
        }
        else if (stream.find("@forceOrder") != std::string::npos) {
            if (d.contains("o")) {
                auto& o = d["o"];
                LiquidationTick l;
                l.symbol = o["s"];
                l.price = std::stod(o["p"].get<std::string>());
                l.qty = std::stod(o["q"].get<std::string>());
                l.notional = l.price * l.qty;
                l.is_long = (o["S"] == "SELL");
                l.ts_ns = ts_ns;
                
                std::lock_guard<std::mutex> lock(handler_mtx_);
                if (liq_handler_) liq_handler_(l);
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[BINANCE] Parse error: " << e.what() << "\n";
    }
}
