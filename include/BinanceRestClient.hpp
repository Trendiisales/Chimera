#pragma once

#include <string>
#include <map>
#include <cstdint>

class BinanceRestClient {
public:
    BinanceRestClient(const std::string& api_key,
                      const std::string& api_secret,
                      const std::string& base_url = "https://api.binance.com");

    // Account / listen key
    std::string create_listen_key();
    bool keepalive_listen_key(const std::string& listen_key);

    // Orders
    std::string place_market_order(const std::string& symbol,
                                   const std::string& side,
                                   double quantity);

    std::string place_limit_order(const std::string& symbol,
                                  const std::string& side,
                                  double quantity,
                                  double price);

private:
    std::string api_key_;
    std::string api_secret_;
    std::string base_url_;

    std::string hmac_sha256(const std::string& payload) const;
    std::string http_request(const std::string& method,
                             const std::string& path,
                             const std::string& query,
                             const std::map<std::string, std::string>& headers = {});
};
