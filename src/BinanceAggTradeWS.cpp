#include "BinanceAggTradeWS.hpp"
#include <libwebsockets.h>
#include <iostream>
#include <cstring>
#include <sstream>

static double parse_double(const std::string& s) {
    try { return std::stod(s); } catch(...) { return 0.0; }
}

static uint64_t parse_uint64(const std::string& s) {
    try { return std::stoull(s); } catch(...) { return 0; }
}

static std::string extract_field(const std::string& json, const std::string& key) {
    size_t pos = json.find("\"" + key + "\":");
    if (pos == std::string::npos) return "";
    pos += key.length() + 3;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '"')) pos++;
    size_t end = pos;
    while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != '"') end++;
    return json.substr(pos, end - pos);
}

static int callback_aggtrade(struct lws* wsi,
                              enum lws_callback_reasons reason,
                              void* user,
                              void* in,
                              size_t len) {
    auto* self = static_cast<BinanceAggTradeWS*>(lws_context_user(lws_get_context(wsi)));

    if (reason == LWS_CALLBACK_CLIENT_RECEIVE && self) {
        try {
            std::string msg((char*)in, len);
            if (msg.find("\"e\":\"aggTrade\"") != std::string::npos) {
                AggTrade trade;
                trade.trade_id = parse_uint64(extract_field(msg, "a"));
                trade.price = parse_double(extract_field(msg, "p"));
                trade.qty = parse_double(extract_field(msg, "q"));
                trade.trade_time = parse_uint64(extract_field(msg, "T"));
                trade.is_buyer_maker = extract_field(msg, "m") == "true";

                if (trade.price > 0 && trade.qty > 0) {
                    self->callback_(trade);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[AGGTRADE_WS] parse error: " << e.what() << "\n";
        }
    }

    return 0;
}

BinanceAggTradeWS::BinanceAggTradeWS(const std::string& symbol, Callback cb)
    : symbol_(symbol), callback_(cb), running_(false) {}

BinanceAggTradeWS::~BinanceAggTradeWS() {
    stop();
}

void BinanceAggTradeWS::start() {
    running_ = true;
    thread_ = std::thread(&BinanceAggTradeWS::run, this);
}

void BinanceAggTradeWS::stop() {
    running_ = false;
    if (thread_.joinable())
        thread_.join();
}

void BinanceAggTradeWS::run() {
    struct lws_protocols protocols[] = {
        { "binance-aggtrade", callback_aggtrade, 0, 8192, 0, nullptr, 0 },
        { nullptr, nullptr, 0, 0, 0, nullptr, 0 }
    };

    struct lws_context_creation_info info{};
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.user = this;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    struct lws_context* ctx = lws_create_context(&info);
    if (!ctx) {
        std::cerr << "[AGGTRADE_WS] Failed to create context\n";
        return;
    }

    std::string stream = symbol_;
    for (auto& c : stream) c = std::tolower(c);
    std::string path = "/ws/" + stream + "@aggTrade";

    struct lws_client_connect_info ccinfo{};
    ccinfo.context = ctx;
    ccinfo.address = "stream.binance.com";
    ccinfo.port = 9443;
    ccinfo.path = path.c_str();
    ccinfo.host = ccinfo.address;
    ccinfo.origin = ccinfo.address;
    ccinfo.protocol = protocols[0].name;
    ccinfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;

    lws_client_connect_via_info(&ccinfo);
    std::cout << "[AGGTRADE_WS] ✅ Connected to " << path << "\n";

    while (running_) {
        lws_service(ctx, 100);
    }

    lws_context_destroy(ctx);
}
