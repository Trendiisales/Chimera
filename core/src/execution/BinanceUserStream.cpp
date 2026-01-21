#include "chimera/execution/BinanceUserStream.hpp"

#include <curl/curl.h>
#include <iostream>
#include <chrono>

using tcp = boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;
namespace websocket = boost::beast::websocket;
namespace beast = boost::beast;
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

BinanceUserStream::BinanceUserStream(
    const std::string& key,
    const std::string& secret
) : api_key(key),
    api_secret(secret) {}

BinanceUserStream::~BinanceUserStream() {
    stop();
}

void BinanceUserStream::start() {
    running = true;
    listen_key = createListenKey();

    ws_thread = std::thread(
        &BinanceUserStream::worker,
        this
    );

    keepalive_thread = std::thread(
        &BinanceUserStream::keepAlive,
        this
    );
}

void BinanceUserStream::stop() {
    running = false;

    if (ws_thread.joinable()) {
        ws_thread.join();
    }
    if (keepalive_thread.joinable()) {
        keepalive_thread.join();
    }
}

std::string BinanceUserStream::createListenKey() {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response;
    struct curl_slist* headers = nullptr;

    std::string h = "X-MBX-APIKEY: " + api_key;
    headers = curl_slist_append(headers, h.c_str());

    curl_easy_setopt(
        curl,
        CURLOPT_URL,
        "https://api.binance.com/api/v3/userDataStream"
    );
    curl_easy_setopt(
        curl,
        CURLOPT_POST,
        1L
    );
    curl_easy_setopt(
        curl,
        CURLOPT_HTTPHEADER,
        headers
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

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "[USERSTREAM] Failed to get listenKey\n";
        return "";
    }

    auto j = json::parse(response).as_object();
    return j["listenKey"].as_string().c_str();
}

void BinanceUserStream::keepAlive() {
    while (running) {
        std::this_thread::sleep_for(
            std::chrono::minutes(30)
        );

        if (!running) break;

        std::string url =
            "https://api.binance.com/api/v3/userDataStream?listenKey=" +
            listen_key;

        CURL* curl = curl_easy_init();
        if (!curl) continue;

        struct curl_slist* headers = nullptr;
        std::string h = "X-MBX-APIKEY: " + api_key;
        headers = curl_slist_append(headers, h.c_str());

        curl_easy_setopt(
            curl,
            CURLOPT_URL,
            url.c_str()
        );
        curl_easy_setopt(
            curl,
            CURLOPT_CUSTOMREQUEST,
            "PUT"
        );
        curl_easy_setopt(
            curl,
            CURLOPT_HTTPHEADER,
            headers
        );

        curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
}

void BinanceUserStream::worker() {
    while (running) {
        try {
            boost::asio::io_context ioc;
            ssl::context ctx{ssl::context::tlsv12_client};
            ctx.set_default_verify_paths();

            tcp::resolver resolver{ioc};
            auto const results =
                resolver.resolve(
                    "stream.binance.com",
                    "9443"
                );

            websocket::stream<
                ssl::stream<tcp::socket>
            > ws{ioc, ctx};

            boost::asio::connect(
                ws.next_layer().next_layer(),
                results.begin(),
                results.end()
            );

            ws.next_layer().handshake(
                ssl::stream_base::client
            );

            std::string host = "stream.binance.com";
            std::string target = "/ws/" + listen_key;

            ws.handshake(host, target);

            beast::flat_buffer buffer;

            while (running) {
                ws.read(buffer);
                std::string msg =
                    beast::buffers_to_string(
                        buffer.data()
                    );
                buffer.consume(buffer.size());

                auto j = json::parse(msg).as_object();

                if (!j.contains("e")) continue;

                std::string event =
                    j["e"].as_string().c_str();

                if (event == "executionReport") {
                    ExecutionUpdate u;
                    u.symbol =
                        j["s"].as_string().c_str();
                    u.client_id =
                        j["c"].as_string().c_str();
                    u.status =
                        j["X"].as_string().c_str();
                    u.filled_qty =
                        std::stod(
                            j["z"].as_string().c_str()
                        );
                    u.fill_price =
                        std::stod(
                            j["L"].as_string().c_str()
                        );
                    u.is_buy =
                        j["S"].as_string() == "BUY";

                    if (on_execution) {
                        on_execution(u);
                    }
                }

                if (event == "ACCOUNT_UPDATE") {
                    auto& B =
                        j["B"].as_array();
                    for (auto& bal : B) {
                        AccountUpdate a;
                        a.asset =
                            bal.at("a")
                                .as_string()
                                .c_str();
                        a.free =
                            std::stod(
                                bal.at("f")
                                    .as_string()
                                    .c_str()
                            );
                        a.locked =
                            std::stod(
                                bal.at("l")
                                    .as_string()
                                    .c_str()
                            );

                        if (on_account) {
                            on_account(a);
                        }
                    }
                }
            }

        } catch (const std::exception& e) {
            std::cerr
                << "[USERSTREAM] Error: "
                << e.what()
                << "\nReconnecting in 5s...\n";

            std::this_thread::sleep_for(
                std::chrono::seconds(5)
            );
        }
    }
}

}
