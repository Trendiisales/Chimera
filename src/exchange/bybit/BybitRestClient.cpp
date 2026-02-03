#include "exchange/bybit/BybitRestClient.hpp"
#include <stdexcept>
#include <iostream>
#include <thread>
#include <chrono>

using namespace chimera;

// ---------------------------------------------------------------------------
// Bybit V5 REST — signed request flow:
//   1. Compute timestamp (epoch milliseconds)
//   2. Signing payload: query string for GET, JSON body for POST
//   3. Pre-sign: apiKey + recvWindow + timestamp + payload
//   4. HMAC-SHA256(secret, pre-sign) → hex signature
//   5. Headers: X-BYBIT-API-KEY, X-BYBIT-API-SIGN, X-BYBIT-API-TIMESTAMP,
//              X-BYBIT-API-RECV-WINDOW, Content-Type: application/json
// ---------------------------------------------------------------------------

BybitRestClient::BybitRestClient(const std::string& base_url, const BybitAuth& auth)
    : base_(base_url), auth_(auth) {
    curl_ = curl_easy_init();
    if (!curl_) throw std::runtime_error("[BYBIT REST] curl_easy_init failed");
}

BybitRestClient::~BybitRestClient() {
    if (curl_) { curl_easy_cleanup(curl_); curl_ = nullptr; }
}

size_t BybitRestClient::write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    std::string* out = reinterpret_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string BybitRestClient::perform(const std::string& method,
                                      const std::string& path,
                                      const std::string& query,
                                      const std::string& body) {
    std::string timestamp = BybitAuth::now_ms();

    // Signing payload: query string for GET, body for POST
    const std::string& sign_payload = (method == "GET") ? query : body;
    std::string signature = auth_.sign(timestamp, sign_payload, RECV_WINDOW);

    // Build URL — append query string if present
    std::string url = base_ + path;
    if (!query.empty()) url += "?" + query;

    // ---------------------------------------------------------------------------
    // Headers — all five required by Bybit V5 for signed endpoints
    // ---------------------------------------------------------------------------
    std::string hdr_key       = "X-BYBIT-API-KEY: "         + auth_.api_key();
    std::string hdr_sign      = "X-BYBIT-API-SIGN: "        + signature;
    std::string hdr_timestamp = "X-BYBIT-API-TIMESTAMP: "   + timestamp;
    std::string hdr_window    = "X-BYBIT-API-RECV-WINDOW: " + std::string(RECV_WINDOW);
    std::string hdr_content   = "Content-Type: application/json";

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, hdr_key.c_str());
    headers = curl_slist_append(headers, hdr_sign.c_str());
    headers = curl_slist_append(headers, hdr_timestamp.c_str());
    headers = curl_slist_append(headers, hdr_window.c_str());
    headers = curl_slist_append(headers, hdr_content.c_str());

    std::string response;

    curl_easy_setopt(curl_, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER,     headers);
    curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST,  method.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION,  write_cb);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA,      &response);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT,        5L);
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 3L);

    if (method == "POST") {
        curl_easy_setopt(curl_, CURLOPT_POST,          1L);
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS,    body.c_str());
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    } else {
        curl_easy_setopt(curl_, CURLOPT_POST,          0L);
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS,    nullptr);
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, 0L);
    }

    // ---------------------------------------------------------------------------
    // Retry loop — 3 attempts with exponential backoff. Matches Binance pattern.
    // ---------------------------------------------------------------------------
    static constexpr int MAX_RETRIES = 3;
    for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
        response.clear();
        CURLcode res = curl_easy_perform(curl_);
        if (res == CURLE_OK) {
            curl_slist_free_all(headers);
            return response;
        }
        if (attempt < MAX_RETRIES - 1) {
            std::cout << "[BYBIT REST] Retry " << (attempt + 1) << "/" << MAX_RETRIES
                      << " (" << curl_easy_strerror(res) << ")\n";
            std::this_thread::sleep_for(
                std::chrono::milliseconds(100 * (1 << attempt)));
        }
    }

    curl_slist_free_all(headers);
    throw std::runtime_error("[BYBIT REST] failed after retries");
}

// ---------------------------------------------------------------------------
// V5 API endpoints — category=linear for perpetual futures
// ---------------------------------------------------------------------------

std::string BybitRestClient::get_positions() {
    return perform("GET", "/v5/position/list", "category=linear&limit=200");
}

std::string BybitRestClient::get_open_orders() {
    return perform("GET", "/v5/order/realtime", "category=linear");
}

std::string BybitRestClient::place_order(const std::string& body) {
    return perform("POST", "/v5/order/submit", "", body);
}

std::string BybitRestClient::cancel_order(const std::string& body) {
    return perform("POST", "/v5/order/cancel", "", body);
}
