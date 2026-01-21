#include "chimera/infra/ClockSync.hpp"

#include <curl/curl.h>
#include <boost/json.hpp>
#include <chrono>

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

ClockSync::ClockSync(
    const std::string& rest_url
) : url(rest_url) {}

void ClockSync::refresh() {
    CURL* curl = curl_easy_init();
    if (!curl) return;

    std::string response;
    curl_easy_setopt(
        curl,
        CURLOPT_URL,
        (url + "/api/v3/time").c_str()
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

    if (curl_easy_perform(curl) == CURLE_OK) {
        auto obj =
            json::parse(response).as_object();
        int64_t server =
            obj["serverTime"].as_int64();

        int64_t local =
            std::chrono::duration_cast<
                std::chrono::milliseconds
            >(
                std::chrono::system_clock::now()
                    .time_since_epoch()
            ).count();

        offset.store(server - local);
    }

    curl_easy_cleanup(curl);
}

int64_t ClockSync::offsetMs() const {
    return offset.load();
}

int64_t ClockSync::nowMs() const {
    int64_t local =
        std::chrono::duration_cast<
            std::chrono::milliseconds
        >(
            std::chrono::system_clock::now()
                .time_since_epoch()
        ).count();

    return local + offset.load();
}

}
