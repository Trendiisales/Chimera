#pragma once
#include <string>
#include <curl/curl.h>
#include "exchange/binance/BinanceAuth.hpp"

namespace chimera {

class BinanceRestClient {
public:
    BinanceRestClient(const std::string& base_url, const BinanceAuth& auth);
    ~BinanceRestClient();

    std::string create_listen_key();
    void        keepalive_listen_key(const std::string& key);
    std::string get_account_snapshot();
    std::string get_open_orders();

    // ---------------------------------------------------------------------------
    // Cancel federation sweep fallback — fire-and-forget.
    // Called ONLY by ExecutionRouter cancel federation sweep when system is dying.
    // Normal hot-path cancels go through BinanceWSExecution.
    // ---------------------------------------------------------------------------
    bool cancel_order_by_client_id(const std::string& symbol,
                                    const std::string& client_id);

private:
    static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata);

    std::string perform(const std::string& method, const std::string& path,
                        const std::string& query, bool signed_req);

    // Persistent CURL handle — reused across all REST calls.
    // Drop 12 did curl_easy_init per request which adds latency at HFT frequency.
    CURL*       curl_{nullptr};
    std::string base_;
    const BinanceAuth& auth_;

    // ---------------------------------------------------------------------------
    // Endpoint mode: detected from base_ URL at construction time.
    // fapi.binance.com → futures (USDT-M perps). /fapi/v1/ paths.
    // api.binance.com  → spot.                   /api/v3/  paths.
    // All methods use futures_ to select the correct path at runtime.
    // ---------------------------------------------------------------------------
    bool futures_{false};
};

} // namespace chimera
