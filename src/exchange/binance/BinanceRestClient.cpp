#include "exchange/binance/BinanceRestClient.hpp"
#include <stdexcept>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <thread>

using namespace chimera;

// ---------------------------------------------------------------------------
static std::string now_ms() {
    using namespace std::chrono;
    return std::to_string(
        duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()
        ).count()
    );
}
// ---------------------------------------------------------------------------

BinanceRestClient::BinanceRestClient(const std::string& base_url, const BinanceAuth& auth)
    : base_(base_url), auth_(auth) {
    futures_ = (base_url.find("fapi") != std::string::npos);
    std::cout << "[REST] Mode: " << (futures_ ? "FUTURES (USDT-M)" : "SPOT") << " base=" << base_ << "\n";
    curl_ = curl_easy_init();
    if (!curl_) throw std::runtime_error("[REST] curl_easy_init failed");
}

BinanceRestClient::~BinanceRestClient() {
    if (curl_) {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
    // Note: curl_global_cleanup() is intentionally NOT called here.
    // It must only be called once per process, after ALL curl handles are gone.
    // In Chimera there is exactly one BinanceRestClient per lifetime, so this
    // is safe — the handle is cleaned up, and global cleanup happens at process exit.
}

size_t BinanceRestClient::write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    std::string* out = reinterpret_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string BinanceRestClient::perform(const std::string& method,
                                        const std::string& path,
                                        const std::string& query,
                                        bool signed_req) {
    std::string full_query = query;

    if (signed_req) {
        // Append timestamp
        std::string ts_param = "timestamp=" + now_ms();
        if (!full_query.empty()) full_query += "&";
        full_query += ts_param;

        // Sign everything so far, append signature
        full_query += "&signature=" + auth_.sign(full_query);
    }

    std::string url = base_ + path;
    if (!full_query.empty()) url += "?" + full_query;

    // --- Headers ---
    std::string response;
    struct curl_slist* headers = nullptr;
    std::string key_hdr = "X-MBX-APIKEY: " + auth_.api_key();
    headers = curl_slist_append(headers, key_hdr.c_str());

    // --- Configure persistent handle ---
    // Reset options that vary per call, keep connection alive.
    curl_easy_setopt(curl_, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER,     headers);
    curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST,  method.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION,  write_cb);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA,      &response);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT,        5L);
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 3L);

    // POST needs POSTFIELDSIZE=0 when there's no body (params are in URL)
    if (method == "POST") {
        curl_easy_setopt(curl_, CURLOPT_POST, 1L);
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, 0L);
    } else {
        // Reset POST state — persistent handle retains options across calls.
        // Without this, a GET after a POST runs as POST and fails.
        curl_easy_setopt(curl_, CURLOPT_POST, 0L);
    }

    // --- Retry loop: 3 attempts with exponential backoff ---
    // Binance can return transient 503/429. Without retry, a single network
    // hiccup kills the reconcile path and blocks arming.
    static constexpr int MAX_RETRIES = 3;
    for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
        response.clear();
        CURLcode res = curl_easy_perform(curl_);
        if (res == CURLE_OK) {
            curl_slist_free_all(headers);
            return response;
        }
        if (attempt < MAX_RETRIES - 1) {
            std::cout << "[REST] Retry " << (attempt+1) << "/" << MAX_RETRIES
                      << " (" << curl_easy_strerror(res) << ")\n";
            std::this_thread::sleep_for(
                std::chrono::milliseconds(100 * (1 << attempt)));  // 100ms, 200ms, 400ms
        }
    }

    curl_slist_free_all(headers);
    throw std::runtime_error(std::string("[REST] failed after retries"));
}

// ---------------------------------------------------------------------------
// Listen key management (unsigned — Binance doesn't require signature for these)
// ---------------------------------------------------------------------------
std::string BinanceRestClient::create_listen_key() {
    // Spot:    POST /api/v3/userDataStream
    // Futures: POST /fapi/v1/listenKey
    return perform("POST", futures_ ? "/fapi/v1/listenKey" : "/api/v3/userDataStream", "", false);
}

void BinanceRestClient::keepalive_listen_key(const std::string& key) {
    // Spot:    PUT /api/v3/userDataStream
    // Futures: PUT /fapi/v1/listenKey
    perform("PUT", futures_ ? "/fapi/v1/listenKey" : "/api/v3/userDataStream", "listenKey=" + key, false);
}

std::string BinanceRestClient::get_account_snapshot() {
    // Spot: /api/v3/account   Futures: /fapi/v2/account
    return perform("GET", futures_ ? "/fapi/v2/account" : "/api/v3/account", "", true);
}

std::string BinanceRestClient::get_open_orders() {
    // Spot: /api/v3/openOrders   Futures: /fapi/v1/openOrders
    return perform("GET", futures_ ? "/fapi/v1/openOrders" : "/api/v3/openOrders", "", true);
}

// ---------------------------------------------------------------------------
// Cancel federation sweep fallback — fire-and-forget.
// ---------------------------------------------------------------------------
bool BinanceRestClient::cancel_order_by_client_id(const std::string& symbol,
                                                    const std::string& client_id) {
    // Spot: DELETE /api/v3/order   Futures: DELETE /fapi/v1/order
    std::ostringstream q;
    q << "symbol=" << symbol
      << "&origClientOrderId=" << client_id;

    try {
        std::string resp = perform("DELETE", futures_ ? "/fapi/v1/order" : "/api/v3/order", q.str(), true);
        std::cout << "[REST] Cancel-by-CID sent: " << resp << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cout << "[REST] Cancel-by-CID failed: " << e.what() << "\n";
        return false;
    }
}
