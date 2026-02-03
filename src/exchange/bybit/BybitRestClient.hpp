#pragma once
#include <string>
#include <curl/curl.h>
#include "exchange/bybit/BybitAuth.hpp"

namespace chimera {

class BybitRestClient {
public:
    BybitRestClient(const std::string& base_url, const BybitAuth& auth);
    ~BybitRestClient();

    // Cold-start reconciliation queries
    std::string get_positions();       // GET /v5/position/list
    std::string get_open_orders();     // GET /v5/order/realtime

    // Execution — only reached when LiveArmSystem is armed+verified
    std::string place_order(const std::string& body);   // POST /v5/order/submit
    std::string cancel_order(const std::string& body);  // POST /v5/order/cancel

private:
    static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata);

    // path: endpoint without query string. query: raw query string (no leading ?).
    // body: raw JSON for POST, empty for GET.
    // Signing payload is query for GET, body for POST.
    std::string perform(const std::string& method,
                        const std::string& path,
                        const std::string& query,
                        const std::string& body = "");

    CURL*       curl_{nullptr};
    std::string base_;
    const BybitAuth& auth_;
    static constexpr const char* RECV_WINDOW = "5000";
};

} // namespace chimera
