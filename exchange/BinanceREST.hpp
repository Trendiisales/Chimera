#pragma once
#include <string>

class BinanceREST {
    std::string api_key;
    std::string api_secret;
    bool dry;

    std::string sign(const std::string& query);

public:
    BinanceREST(const std::string& key, const std::string& secret, bool dry_run);

    std::string sendOrder(const std::string& symbol,
                          const std::string& side,
                          double qty,
                          double price = 0,
                          bool market = true);

    void cancelAll(const std::string& symbol);
};
