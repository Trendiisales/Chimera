#include "exchange/okx/OKXRestClient.hpp"
#include <stdexcept>
#include <iostream>
#include <thread>
#include <chrono>

using namespace chimera;

// ---------------------------------------------------------------------------
// OKX V5 REST — signed request flow:
//   1. Compute timestamp (epoch seconds)
//   2. Build pre-sign string: timestamp + METHOD + path + body
//   3. HMAC-SHA256(secret, pre-sign) → base64 signature
//   4. Headers: OK-ACCESS-KEY, OK-ACCESS-SIGN, OK-ACCESS-TIMESTAMP,
//              OK-ACCESS-PASSPHRASE, Content-Type: application/json
//
// GET:  path includes query string (e.g. /api/v5/position/positions?instType=SWAP)
//       body is empty
// POST: path is endpoint only (e.g. /api/v5/trade/order)
//       body is raw JSON payload — included in both signature and request
// ---------------------------------------------------------------------------

OKXRestClient::OKXRestClient(const std::string& base_url, const OKXAuth& auth)
    : base_(base_url), auth_(auth) {
    curl_ = curl_easy_init();
    if (!curl_) throw std::runtime_error("[OKX REST] curl_easy_init failed");
}

OKXRestClient::~OKXRestClient() {
    if (curl_) { curl_easy_cleanup(curl_); curl_ = nullptr; }
}

size_t OKXRestClient::write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    std::string* out = reinterpret_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string OKXRestClient::perform(const std::string& method,
                                    const std::string& path,
                                    const std::string& body) {
    std::string timestamp = OKXAuth::now_sec();
    std::string signature = auth_.sign(timestamp, method, path, body);

    std::string url = base_ + path;

    // ---------------------------------------------------------------------------
    // Headers — all five required by OKX V5 for signed endpoints
    // ---------------------------------------------------------------------------
    std::string hdr_key        = "OK-ACCESS-KEY: "        + auth_.api_key();
    std::string hdr_sign       = "OK-ACCESS-SIGN: "       + signature;
    std::string hdr_timestamp  = "OK-ACCESS-TIMESTAMP: "  + timestamp;
    std::string hdr_passphrase = "OK-ACCESS-PASSPHRASE: " + auth_.passphrase();
    std::string hdr_content    = "Content-Type: application/json";

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, hdr_key.c_str());
    headers = curl_slist_append(headers, hdr_sign.c_str());
    headers = curl_slist_append(headers, hdr_timestamp.c_str());
    headers = curl_slist_append(headers, hdr_passphrase.c_str());
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
    // Retry loop — OKX can return transient 503/429. 3 attempts, exponential
    // backoff (100ms / 200ms / 400ms). Matches BinanceRestClient pattern.
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
            std::cout << "[OKX REST] Retry " << (attempt + 1) << "/" << MAX_RETRIES
                      << " (" << curl_easy_strerror(res) << ")\n";
            std::this_thread::sleep_for(
                std::chrono::milliseconds(100 * (1 << attempt)));
        }
    }

    curl_slist_free_all(headers);
    throw std::runtime_error("[OKX REST] failed after retries");
}

// ---------------------------------------------------------------------------
// V5 API endpoints
// ---------------------------------------------------------------------------

std::string OKXRestClient::get_positions() {
    // instType=SWAP targets perpetual swap positions only
    return perform("GET", "/api/v5/position/positions?instType=SWAP");
}

std::string OKXRestClient::get_open_orders() {
    return perform("GET", "/api/v5/trade/orders-pending?instType=SWAP");
}

std::string OKXRestClient::place_order(const std::string& body) {
    return perform("POST", "/api/v5/trade/order", body);
}

std::string OKXRestClient::cancel_order(const std::string& body) {
    return perform("POST", "/api/v5/trade/cancel-order", body);
}
