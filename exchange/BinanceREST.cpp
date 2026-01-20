#include "BinanceREST.hpp"
#include <curl/curl.h>
#include <openssl/hmac.h>
#include <sstream>
#include <iomanip>
#include <ctime>

static size_t write_cb(void* ptr, size_t size, size_t nmemb, void* data) {
    ((std::string*)data)->append((char*)ptr, size * nmemb);
    return size * nmemb;
}

BinanceREST::BinanceREST(const std::string& k,
                         const std::string& s,
                         bool d)
    : api_key(k), api_secret(s), dry(d) {}

std::string BinanceREST::sign(const std::string& q) {
    unsigned char* digest;
    digest = HMAC(EVP_sha256(),
                  api_secret.c_str(),
                  api_secret.size(),
                  (unsigned char*)q.c_str(),
                  q.size(),
                  nullptr,
                  nullptr);

    std::ostringstream ss;
    for (int i = 0; i < 32; i++)
        ss << std::hex << std::setw(2)
           << std::setfill('0')
           << (int)digest[i];
    return ss.str();
}

std::string BinanceREST::sendOrder(const std::string& symbol,
                                   const std::string& side,
                                   double qty,
                                   double price,
                                   bool market) {
    if (dry) return "DRY_RUN_OK";

    CURL* curl = curl_easy_init();
    std::string result;

    long ts = std::time(nullptr) * 1000;

    std::ostringstream q;
    q << "symbol=" << symbol
      << "&side=" << side
      << "&type=" << (market ? "MARKET" : "LIMIT")
      << "&quantity=" << qty
      << "&timestamp=" << ts;

    if (!market)
        q << "&price=" << price
          << "&timeInForce=GTC";

    std::string sig = sign(q.str());

    std::string url =
        "https://api.binance.com/api/v3/order?" +
        q.str() + "&signature=" + sig;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(
        headers,
        ("X-MBX-APIKEY: " + api_key).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);

    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return result;
}

void BinanceREST::cancelAll(const std::string& symbol) {
    if (dry) return;

    CURL* curl = curl_easy_init();
    long ts = std::time(nullptr) * 1000;

    std::ostringstream q;
    q << "symbol=" << symbol
      << "&timestamp=" << ts;

    std::string sig = sign(q.str());

    std::string url =
        "https://api.binance.com/api/v3/openOrders?" +
        q.str() + "&signature=" + sig;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(
        headers,
        ("X-MBX-APIKEY: " + api_key).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
}
