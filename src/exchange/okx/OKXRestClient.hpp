#pragma once
#include <string>
#include <curl/curl.h>
#include "exchange/okx/OKXAuth.hpp"

namespace chimera {

class OKXRestClient {
public:
    OKXRestClient(const std::string& base_url, const OKXAuth& auth);
    ~OKXRestClient();

    // Cold-start reconciliation queries
    std::string get_positions();       // GET /api/v5/position/positions
    std::string get_open_orders();     // GET /api/v5/trade/orders-pending

    // Execution — only reached when LiveArmSystem is armed+verified
    std::string place_order(const std::string& body);   // POST /api/v5/trade/order
    std::string cancel_order(const std::string& body);  // POST /api/v5/trade/cancel-order

private:
    static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata);

    // Unified request dispatcher. path includes query string for GET.
    // body is raw JSON for POST, empty for GET.
    std::string perform(const std::string& method,
                        const std::string& path,
                        const std::string& body = "");

    CURL*       curl_{nullptr};   // persistent handle — reused across calls
    std::string base_;
    const OKXAuth& auth_;
};

} // namespace chimera
