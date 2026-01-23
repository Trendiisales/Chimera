#include "chimera/safety/ExchangeInfoCache.hpp"
// Suppress unused parameter warnings
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include <curl/curl.h>
#include <boost/json.hpp>
#include <iostream>

namespace json = boost::json;

namespace chimera {

static size_t curlWrite(
    void* contents,
    size_t size,
    size_t nmemb,
    void* userp
) {
    size_t total = size * nmemb;
    std::string* s = static_cast<std::string*>(userp);
    s->append((char*)contents, total);
    return total;
}

ExchangeInfoCache::ExchangeInfoCache(
    const std::string& rest_url
) : url(rest_url) {}

void ExchangeInfoCache::refresh() {
    CURL* curl = curl_easy_init();
    if (!curl) return;

    std::string response;

    curl_easy_setopt(
        curl,
        CURLOPT_URL,
        (url + "/api/v3/exchangeInfo").c_str()
    );
    curl_easy_setopt(
        curl,
        CURLOPT_WRITEFUNCTION,
        curlWrite
    );
    curl_easy_setopt(
        curl,
        CURLOPT_WRITEDATA,
        &response
    );

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr
            << "[EXINFO] Failed to fetch exchangeInfo\n";
        return;
    }

    parse(response);
}

void ExchangeInfoCache::parse(
    const std::string& body
) {
    auto root = json::parse(body).as_object();
    auto syms = root["symbols"].as_array();

    std::lock_guard<std::mutex> lock(mtx);
    map.clear();

    for (auto& s : syms) {
        auto obj = s.as_object();
        std::string sym =
            obj["symbol"].as_string().c_str();

        SymbolRules r;

        for (auto& f : obj["filters"].as_array()) {
            auto fo = f.as_object();
            std::string type =
                fo["filterType"]
                    .as_string()
                    .c_str();

            if (type == "LOT_SIZE") {
                r.min_qty =
                    std::stod(
                        fo["minQty"]
                            .as_string()
                            .c_str()
                    );
                r.step_size =
                    std::stod(
                        fo["stepSize"]
                            .as_string()
                            .c_str()
                    );
            }

            if (type == "PRICE_FILTER") {
                r.tick_size =
                    std::stod(
                        fo["tickSize"]
                            .as_string()
                            .c_str()
                    );
            }

            if (type == "MIN_NOTIONAL") {
                r.min_notional =
                    std::stod(
                        fo["minNotional"]
                            .as_string()
                            .c_str()
                    );
            }
        }

        map[sym] = r;
    }
}

bool ExchangeInfoCache::has(
    const std::string& symbol
) const {
    std::lock_guard<std::mutex> lock(mtx);
    return map.count(symbol) > 0;
}

const SymbolRules&
ExchangeInfoCache::rules(
    const std::string& symbol
) const {
    static SymbolRules empty;
    std::lock_guard<std::mutex> lock(mtx);

    auto it = map.find(symbol);
    if (it == map.end()) {
        return empty;
    }

    return it->second;
}

}
