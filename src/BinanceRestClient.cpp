#include "BinanceRestClient.hpp"

#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <curl/curl.h>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <chrono>

static size_t curl_write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    std::string* out = static_cast<std::string*>(userp);
    out->append(static_cast<char*>(contents), total);
    return total;
}

BinanceRestClient::BinanceRestClient(const std::string& api_key,
                                     const std::string& api_secret,
                                     const std::string& base_url)
    : api_key_(api_key),
      api_secret_(api_secret),
      base_url_(base_url) {}

std::string BinanceRestClient::hmac_sha256(const std::string& payload) const {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;

    HMAC(EVP_sha256(),
         api_secret_.data(),
         static_cast<int>(api_secret_.size()),
         reinterpret_cast<const unsigned char*>(payload.data()),
         payload.size(),
         digest,
         &len);

    std::ostringstream oss;
    for (unsigned int i = 0; i < len; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(digest[i]);
    }
    return oss.str();
}

std::string BinanceRestClient::http_request(const std::string& method,
                                            const std::string& path,
                                            const std::string& query,
                                            const std::map<std::string, std::string>& headers) {
    CURL* curl = curl_easy_init();
    if (!curl)
        throw std::runtime_error("curl_easy_init failed");

    std::string response;
    std::string url = base_url_ + path;
    if (!query.empty())
        url += "?" + query;

    struct curl_slist* header_list = nullptr;
    header_list = curl_slist_append(header_list, ("X-MBX-APIKEY: " + api_key_).c_str());
    for (const auto& kv : headers)
        header_list = curl_slist_append(header_list, (kv.first + ": " + kv.second).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
    } else if (method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    }

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        throw std::runtime_error("curl_easy_perform failed");

    return response;
}

static uint64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
}

std::string BinanceRestClient::create_listen_key() {
    return http_request("POST", "/api/v3/userDataStream", "");
}

bool BinanceRestClient::keepalive_listen_key(const std::string& listen_key) {
    try {
        http_request("PUT", "/api/v3/userDataStream", "listenKey=" + listen_key);
        return true;
    } catch (...) {
        return false;
    }
}

std::string BinanceRestClient::place_market_order(const std::string& symbol,
                                                   const std::string& side,
                                                   double quantity) {
    std::ostringstream q;
    q << "symbol=" << symbol
      << "&side=" << side
      << "&type=MARKET"
      << "&quantity=" << quantity
      << "&timestamp=" << now_ms();

    std::string sig = hmac_sha256(q.str());
    q << "&signature=" << sig;

    return http_request("POST", "/api/v3/order", q.str());
}

std::string BinanceRestClient::place_limit_order(const std::string& symbol,
                                                  const std::string& side,
                                                  double quantity,
                                                  double price) {
    std::ostringstream q;
    q << "symbol=" << symbol
      << "&side=" << side
      << "&type=LIMIT"
      << "&timeInForce=GTC"
      << "&quantity=" << quantity
      << "&price=" << price
      << "&timestamp=" << now_ms();

    std::string sig = hmac_sha256(q.str());
    q << "&signature=" << sig;

    return http_request("POST", "/api/v3/order", q.str());
}
