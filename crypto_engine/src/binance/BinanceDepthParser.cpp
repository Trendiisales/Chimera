#include "binance/BinanceDepthParser.hpp"

namespace binance {

static uint64_t seq = 1000;

static std::string extract_symbol(const std::string& raw) {
    const char* k = "\"s\":\"";
    auto p = raw.find(k);
    if (p == std::string::npos)
        return "UNKNOWN";
    p += 5;
    auto e = raw.find('"', p);
    if (e == std::string::npos)
        return "UNKNOWN";
    return raw.substr(p, e - p);
}

std::optional<DepthDelta> BinanceDepthParser::parse(const std::string& raw) {
    DepthDelta d;
    d.symbol = extract_symbol(raw);

    d.U = seq;
    d.u = seq;
    seq++;

    return d;
}

}
