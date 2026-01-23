#include "chimera/infra/Clock.hpp"
#include "chimera/execution/BinanceIO.hpp"
#include "chimera/execution/Hash.hpp"  // ← ADDED FOR HASH COMPUTATION

#include <chrono>
#include <thread>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>

// For real WebSocket - using boost::beast
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/json.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;
using boost::beast::get_lowest_layer;

namespace chimera {

static size_t curlWrite(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

BinanceIO::BinanceIO(const BinanceConfig& cfg)
    : config(cfg), running(false) {}

BinanceIO::~BinanceIO() {
    disconnect();
}

void BinanceIO::connect() {
    running = true;
    ws_worker = std::thread(&BinanceIO::wsThread, this);
}

void BinanceIO::disconnect() {
    running = false;
    if (ws_worker.joinable()) {
        ws_worker.join();
    }
}

void BinanceIO::subscribeMarketData(
    const std::vector<std::string>& syms
) {
    std::lock_guard<std::mutex> lock(sym_mutex);
    symbols = syms;
}

uint64_t BinanceIO::nowMs() const {
    return std::chrono::duration_cast<
        std::chrono::milliseconds
    >(
        chimera::infra::now().time_since_epoch()
    ).count();
}

std::string BinanceIO::signQuery(const std::string& query) {
    unsigned char* digest;
    digest = HMAC(
        EVP_sha256(),
        config.api_secret.c_str(),
        config.api_secret.size(),
        (unsigned char*)query.c_str(),
        query.size(),
        NULL,
        NULL
    );

    std::ostringstream oss;
    for (int i = 0; i < 32; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << (int)digest[i];
    }
    return oss.str();
}

void BinanceIO::poll() {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(10)
    );
}

void BinanceIO::sendOrder(const OrderRequest& req) {
    if (config.shadow_mode) {
        OrderUpdate up;
        up.client_id = req.client_id;
        up.exchange_id = "BINANCE-SHADOW-" + req.client_id;
        up.filled_qty = req.qty;
        up.avg_price = req.price;
        up.is_final = true;
        up.status = "FILLED";

        if (on_order_update) {
            on_order_update(up);
        }
        return;
    }

    restSendOrder(req);
}

void BinanceIO::cancelOrder(
    const std::string& client_id
) {
    if (config.shadow_mode) return;
    restCancelOrder(client_id);
}

void BinanceIO::restSendOrder(
    const OrderRequest& req
) {
    std::lock_guard<std::mutex> lock(rate_mutex);

    uint64_t now = nowMs();
    if (now - last_rest_ts < 100) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(100)
        );
    }
    last_rest_ts = now;

    std::ostringstream query;
    query << "symbol=" << req.symbol
          << "&side=" << (req.is_buy ? "BUY" : "SELL")
          << "&type=LIMIT"
          << "&timeInForce=GTC"
          << "&quantity=" << req.qty
          << "&price=" << req.price
          << "&newClientOrderId=" << req.client_id
          << "&timestamp=" << nowMs();

    std::string sig = signQuery(query.str());

    std::string url =
        config.rest_url +
        "/api/v3/order?" +
        query.str() +
        "&signature=" + sig;

    CURL* curl = curl_easy_init();
    if (!curl) return;

    std::string response;
    struct curl_slist* headers = NULL;
    std::string api_hdr = "X-MBX-APIKEY: " + config.api_key;
    headers = curl_slist_append(headers, api_hdr.c_str());

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
}

void BinanceIO::restCancelOrder(
    const std::string& client_id
) {
    std::lock_guard<std::mutex> lock(rate_mutex);

    uint64_t now = nowMs();
    std::ostringstream query;
    query << "origClientOrderId=" << client_id
          << "&timestamp=" << now;

    std::string sig = signQuery(query.str());

    std::string url =
        config.rest_url +
        "/api/v3/order?" +
        query.str() +
        "&signature=" + sig;

    CURL* curl = curl_easy_init();
    if (!curl) return;

    struct curl_slist* headers = NULL;
    std::string api_hdr = "X-MBX-APIKEY: " + config.api_key;
    headers = curl_slist_append(headers, api_hdr.c_str());

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
}

// REAL WEBSOCKET IMPLEMENTATION
void BinanceIO::wsThread() {
    while (running) {
        try {
            // Get local copy of symbols
            std::vector<std::string> local_syms;
            {
                std::lock_guard<std::mutex> lock(sym_mutex);
                local_syms = symbols;
            }

            if (local_syms.empty()) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            // Build stream names: btcusdt@bookTicker/ethusdt@bookTicker
            std::ostringstream stream_params;
            for (size_t i = 0; i < local_syms.size(); ++i) {
                std::string sym_lower = local_syms[i];
                for (auto& c : sym_lower) c = std::tolower(c);
                
                stream_params << sym_lower << "@bookTicker";
                if (i < local_syms.size() - 1) {
                    stream_params << "/";
                }
            }

            std::string stream = stream_params.str();
            std::string host = "stream.binance.com";
            std::string port = "9443";
            std::string path = "/stream?streams=" + stream;

            std::cout << "[BINANCE WS] Connecting to: " << host << ":" << port << path << std::endl;

            // SSL context
            net::io_context ioc;
            ssl::context ctx{ssl::context::tlsv12_client};
            ctx.set_default_verify_paths();

            // Resolver and WebSocket stream
            tcp::resolver resolver{ioc};
            websocket::stream<ssl::stream<tcp::socket>> ws{ioc, ctx};

            // Resolve and connect
            auto const results = resolver.resolve(host, port);
            net::connect(get_lowest_layer(ws), results);

            // SNI and SSL handshake
            if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), host.c_str())) {
                throw beast::system_error{
                    beast::error_code{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()},
                    "SSL_set_tlsext_host_name"
                };
            }

            ws.next_layer().handshake(ssl::stream_base::client);

            // WebSocket handshake
            ws.set_option(websocket::stream_base::decorator(
                [](websocket::request_type& req) {
                    req.set(http::field::user_agent, "ChimeraHFT/1.0");
                }
            ));

            ws.handshake(host, path);
            std::cout << "[BINANCE WS] Connected!" << std::endl;

            // Set timeout so read() can be interrupted
            ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));

            // Read loop
            beast::flat_buffer buffer;
            while (running) {
                beast::error_code ec;
                ws.read(buffer, ec);
                
                if (ec == websocket::error::closed || !running) {
                    break;
                }
                
                if (ec) {
                    throw beast::system_error{ec};
                }
                
                std::string msg = beast::buffers_to_string(buffer.data());
                buffer.consume(buffer.size());

                // Parse JSON
                try {
                    auto json = boost::json::parse(msg);
                    auto& obj = json.as_object();

                    if (obj.contains("data")) {
                        auto& data = obj["data"].as_object();
                        
                        std::string symbol = data["s"].as_string().c_str();
                        double bid = std::stod(data["b"].as_string().c_str());
                        double ask = std::stod(data["a"].as_string().c_str());
                        double bid_qty = std::stod(data["B"].as_string().c_str());
                        double ask_qty = std::stod(data["A"].as_string().c_str());

                        // ============================================================
                        // CRITICAL PATCH: Compute symbol_hash at ingestion
                        // ============================================================
                        MarketTick t;
                        t.symbol = symbol;
                        t.symbol_hash = fnv1a_32(symbol);  // ← ADDED: O(1) routing
                        t.bid = bid;
                        t.ask = ask;
                        t.last = (bid + ask) / 2.0;  // Mid price
                        t.bid_size = bid_qty;
                        t.ask_size = ask_qty;
                        t.ts_ns = nowMs() * 1000000ULL;
                        // ============================================================

                        if (on_tick) {
                            on_tick(t);
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[BINANCE WS] Parse error: " << e.what() << std::endl;
                }
            }

            // Close connection gracefully
            beast::error_code ec;
            ws.close(websocket::close_code::normal, ec);

        } catch (const std::exception& e) {
            std::cerr << "[BINANCE WS] Error: " << e.what() << std::endl;
            if (!running) break;
            
            std::cerr << "[BINANCE WS] Reconnecting in 5 seconds..." << std::endl;
            // Interruptible sleep - check running flag every 100ms
            for (int i = 0; i < 50 && running; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
}

}
