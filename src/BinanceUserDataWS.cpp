#include "BinanceUserDataWS.hpp"

#include <libwebsockets.h>
#include <iostream>

static std::string extract_json_string(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    
    pos += search.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '"')) pos++;
    
    size_t end = pos;
    bool in_string = (pos > 0 && json[pos-1] == '"');
    
    while (end < json.length()) {
        if (in_string && json[end] == '"') break;
        if (!in_string && (json[end] == ',' || json[end] == '}')) break;
        end++;
    }
    
    return json.substr(pos, end - pos);
}

static int callback_userdata(struct lws* wsi,
                             enum lws_callback_reasons reason,
                             void* user,
                             void* in,
                             size_t len) {
    auto* self = static_cast<BinanceUserDataWS*>(lws_context_user(lws_get_context(wsi)));

    if (reason == LWS_CALLBACK_CLIENT_RECEIVE && self) {
        try {
            std::string msg((char*)in, len);
            
            if (extract_json_string(msg, "e") == "executionReport") {
                std::string symbol = extract_json_string(msg, "s");
                std::string side   = extract_json_string(msg, "S");
                double price       = std::stod(extract_json_string(msg, "L"));
                double qty         = std::stod(extract_json_string(msg, "l"));
                self->handler_(symbol, side, price, qty);
            }
        } catch (...) {
            std::cerr << "[USERDATA_WS] parse error\n";
        }
    }
    return 0;
}

BinanceUserDataWS::BinanceUserDataWS(const std::string& listen_key,
                                     FillHandler handler)
    : listen_key_(listen_key),
      handler_(handler),
      running_(false) {}

void BinanceUserDataWS::start() {
    running_ = true;
    thread_ = std::thread(&BinanceUserDataWS::run, this);
}

void BinanceUserDataWS::stop() {
    running_ = false;
    if (thread_.joinable())
        thread_.join();
}

void BinanceUserDataWS::run() {
    struct lws_protocols protocols[] = {
        { "binance", callback_userdata, 0, 8192, 0, nullptr, 0 },
        { nullptr, nullptr, 0, 0, 0, nullptr, 0 }
    };

    struct lws_context_creation_info info{};
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.user = this;

    struct lws_context* ctx = lws_create_context(&info);
    if (!ctx) {
        std::cerr << "[USERDATA_WS] Failed to create context\n";
        return;
    }

    std::string path = "/ws/" + listen_key_;

    struct lws_client_connect_info ccinfo{};
    ccinfo.context = ctx;
    ccinfo.address = "stream.binance.com";
    ccinfo.port = 9443;
    ccinfo.path = path.c_str();
    ccinfo.host = ccinfo.address;
    ccinfo.origin = ccinfo.address;
    ccinfo.protocol = protocols[0].name;
    ccinfo.ssl_connection = LCCSCF_USE_SSL;

    lws_client_connect_via_info(&ccinfo);
    
    std::cout << "[USERDATA_WS] Connected\n";

    while (running_) {
        lws_service(ctx, 100);
    }

    lws_context_destroy(ctx);
}
