#pragma once
#include <string>
#include <vector>

namespace binance {

/*
 Authoritative Binance endpoints.
 Spot only. No futures. No testnet.
*/
struct BinanceEndpoints {
    static constexpr const char* WS_BASE =
        "wss://stream.binance.com:9443/stream";

    static constexpr const char* REST_BASE =
        "https://api.binance.com";
};

/*
 Build stream name for symbol depth.
 Example: btcusdt@depth@100ms
*/
inline std::string depth_stream(const std::string& symbol) {
    std::string s;
    s.reserve(symbol.size() + 16);
    for (char c : symbol)
        s.push_back(static_cast<char>(::tolower(c)));
    s += "@depth@100ms";
    return s;
}

/*
 Build combined stream query.
 Example:
 ?streams=btcusdt@depth@100ms/ethusdt@depth@100ms
*/
inline std::string build_stream_query(const std::vector<std::string>& symbols) {
    std::string q = "?streams=";
    bool first = true;

    for (const auto& s : symbols) {
        if (!first)
            q += "/";
        first = false;
        q += depth_stream(s);
    }
    return q;
}

/*
 Full WS URL.
*/
inline std::string build_ws_url(const std::vector<std::string>& symbols) {
    return std::string(BinanceEndpoints::WS_BASE) +
           build_stream_query(symbols);
}

}
