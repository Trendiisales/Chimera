#include "exchange/binance/BinanceWSExecution.hpp"
#include "exchange/binance/BinanceAuth.hpp"
#include "runtime/Context.hpp"
#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <openssl/ssl.h>

using namespace chimera;
using json = nlohmann::json;

namespace asio      = boost::asio;
namespace ssl       = asio::ssl;
namespace beast     = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

// ---------------------------------------------------------------------------
// Binance WS Trading API endpoints:
//   Spot:    wss://ws-api.binance.com:443/ws-api/v3
//   Futures: wss://ws-fapi.binance.com:443/ws-api/v1
//
// Protocol: JSON-RPC over WebSocket.
//   Request:  {"id":"<unique>","method":"order.place","params":{...}}
//   Response: {"id":"<unique>","status":200,"result":{...}}
//
// All signed requests append timestamp + signature to params.
// Signature = HMAC-SHA256 of the canonical query string of params
// (key=value pairs joined by &, alphabetical order NOT required —
//  Binance signs in the order params appear).
// ---------------------------------------------------------------------------

static uint64_t now_us() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<microseconds>(
            high_resolution_clock::now().time_since_epoch()
        ).count()
    );
}

static std::string now_ms_str() {
    using namespace std::chrono;
    return std::to_string(
        duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()
        ).count()
    );
}

// ---------------------------------------------------------------------------
BinanceWSExecution::BinanceWSExecution(Context& ctx)
    : ctx_(ctx) {
    const char* key    = std::getenv("BINANCE_API_KEY");
    const char* secret = std::getenv("BINANCE_API_SECRET");
    api_key_    = key    ? key    : "";
    api_secret_ = secret ? secret : "";

    const char* mode_env = std::getenv("BINANCE_TRADE_MODE");
    futures_ = (mode_env && std::string(mode_env) == "futures");
}

BinanceWSExecution::~BinanceWSExecution() {
    stop();
}

void BinanceWSExecution::start() {
    if (running_.load()) return;
    running_.store(true);
    ws_thread_ = std::thread(&BinanceWSExecution::ws_thread_fn, this);
    std::cout << "[WS_EXEC] Started (" << (futures_ ? "futures" : "spot") << ")\n";
}

void BinanceWSExecution::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (ws_thread_.joinable()) ws_thread_.join();
    connected_.store(false);
    std::cout << "[WS_EXEC] Stopped\n";
}

// ---------------------------------------------------------------------------
// Hot path — called from CORE1. Non-blocking: queues frame, returns immediately.
// ---------------------------------------------------------------------------
void BinanceWSExecution::send_order(const std::string& symbol,
                                     const std::string& side,
                                     double qty, double price,
                                     const std::string& client_id) {
    // Build JSON-RPC frame
    json frame;
    frame["id"]     = client_id.empty() ? std::to_string(now_us()) : client_id;
    frame["method"] = "order.place";

    // Binance WS API expects params as a flat JSON object
    json params_obj;
    params_obj["symbol"]            = symbol;
    params_obj["side"]              = side;
    params_obj["type"]              = "LIMIT";
    params_obj["quantity"]          = std::to_string(qty);
    params_obj["price"]             = std::to_string(price);
    params_obj["timeInForce"]       = "GTC";
    if (!client_id.empty())
        params_obj["newClientOrderId"] = client_id;
    params_obj["timestamp"]         = now_ms_str();

    // Signature is computed over the canonical query string of params
    // (before adding signature itself)
    std::ostringstream canon;
    canon << "symbol=" << symbol
          << "&side=" << side
          << "&type=LIMIT"
          << "&quantity=" << std::fixed << std::setprecision(8) << qty
          << "&price=" << std::fixed << std::setprecision(8) << price
          << "&timeInForce=GTC";
    if (!client_id.empty())
        canon << "&newClientOrderId=" << client_id;
    canon << "&timestamp=" << params_obj["timestamp"].get<std::string>();

    params_obj["signature"] = sign(canon.str());
    frame["params"] = params_obj;

    // Record pending timestamp BEFORE queuing — ensures latency measurement
    // starts at the earliest possible point.
    {
        std::lock_guard<std::mutex> lk(pending_mtx_);
        pending_[client_id] = now_us();
    }

    // Push to outbound queue
    {
        std::lock_guard<std::mutex> lk(outbound_mtx_);
        outbound_.push_back({frame.dump()});
    }
}

void BinanceWSExecution::cancel_order(const std::string& symbol,
                                       const std::string& client_id) {
    std::string ts = now_ms_str();

    std::ostringstream canon;
    canon << "symbol=" << symbol
          << "&origClientOrderId=" << client_id
          << "&timestamp=" << ts;

    json params_obj;
    params_obj["symbol"]               = symbol;
    params_obj["origClientOrderId"]    = client_id;
    params_obj["timestamp"]            = ts;
    params_obj["signature"]            = sign(canon.str());

    json frame;
    frame["id"]     = "cancel_" + client_id;
    frame["method"] = "order.cancel";
    frame["params"] = params_obj;

    // Record pending for latency tracking
    {
        std::lock_guard<std::mutex> lk(pending_mtx_);
        pending_["cancel_" + client_id] = now_us();
    }

    {
        std::lock_guard<std::mutex> lk(outbound_mtx_);
        outbound_.push_back({frame.dump()});
    }
}

// ---------------------------------------------------------------------------
// WS thread — owns Beast SSL stream. Reconnects on disconnect.
// Drains outbound queue on each loop iteration. Reads responses.
// ---------------------------------------------------------------------------
void BinanceWSExecution::ws_thread_fn() {
    ssl::context sslctx{ssl::context::tls_client};
    sslctx.set_default_verify_paths();

    const char* ws_host = futures_ ? "ws-fapi.binance.com" : "ws-api.binance.com";
    // FIX: Spot = /ws-api/v3, Futures = /ws-api/v1.
    // Was hardcoded to v1 for both — spot handshake declined every time.
    const char* ws_path = futures_ ? "/ws-api/v1" : "/ws-api/v3";

    int backoff_s = 1;
    constexpr int MAX_BACKOFF = 30;

    while (running_.load()) {
        try {
            asio::io_context ioc;
            tcp::resolver    resolver(ioc);

            websocket::stream<ssl::stream<tcp::socket>> ws(ioc, sslctx);

            auto results = resolver.resolve(ws_host, "443");
            asio::connect(ws.next_layer().next_layer(), results.begin(), results.end());

            // SNI + SSL handshake
            ws.next_layer().set_verify_mode(ssl::verify_peer);
            SSL_set_tlsext_host_name(
                ws.next_layer().native_handle(),
                ws_host
            );
            ws.next_layer().handshake(ssl::stream_base::client);

            // WebSocket handshake — Binance WS Trading API does NOT require
            // auth headers on connect. Auth is per-request via signature in params.
            ws.handshake(ws_host, ws_path);

            connected_.store(true);
            backoff_s = 1;
            std::cout << "[WS_EXEC] Connected to " << ws_host << ws_path << "\n";

            // Read buffer + completion state for async read
            beast::flat_buffer buffer;
            bool read_pending = false;
            bool read_done    = false;
            std::string read_msg;
            beast::error_code read_ec;

            while (running_.load()) {
                // --- DRAIN OUTBOUND QUEUE ---
                {
                    std::vector<OutboundFrame> batch;
                    {
                        std::lock_guard<std::mutex> lk(outbound_mtx_);
                        batch.swap(outbound_);
                    }
                    for (const auto& frame : batch) {
                        ws.write(asio::buffer(frame.json));
                    }
                }

                // --- ASYNC READ with 50ms timeout ---
                // Kick off async_read if not already pending.
                if (!read_pending) {
                    read_done = false;
                    ws.async_read(buffer, [&](beast::error_code ec, std::size_t) {
                        read_ec  = ec;
                        read_msg = beast::buffers_to_string(buffer.data());
                        buffer.consume(buffer.size());
                        read_done = true;
                    });
                    read_pending = true;
                }

                // Run io_context for up to 50ms — processes any pending async ops.
                ioc.run_for(std::chrono::milliseconds(50));

                if (read_done) {
                    read_pending = false;
                    if (read_ec) {
                        // WS read error — reconnect
                        throw beast::system_error(read_ec);
                    }
                    handle_response(read_msg);
                    read_msg.clear();
                }
            }

            ws.close(websocket::close_code::normal);

        } catch (const std::exception& e) {
            connected_.store(false);
            if (running_.load()) {
                std::cout << "[WS_EXEC] Reconnect in " << backoff_s << "s (" << e.what() << ")\n";
                std::this_thread::sleep_for(std::chrono::seconds(backoff_s));
                backoff_s = std::min(backoff_s * 2, MAX_BACKOFF);
            }
        }
    }

    connected_.store(false);
}

// ---------------------------------------------------------------------------
// Parse response frame. Update latency or fire rejection.
// ---------------------------------------------------------------------------
void BinanceWSExecution::handle_response(const std::string& msg) {
    try {
        json resp = json::parse(msg);

        std::string id = resp.contains("id") ? resp["id"].get<std::string>() : "";
        int status = resp.contains("status") ? resp["status"].get<int>() : 0;

        if (id.empty()) return;  // ping/pong or malformed

        // --- LATENCY MEASUREMENT ---
        uint64_t t_ack = now_us();
        {
            std::lock_guard<std::mutex> lk(pending_mtx_);
            auto it = pending_.find(id);
            if (it != pending_.end()) {
                uint64_t latency_us = t_ack - it->second;
                ctx_.latency.update_latency_us(latency_us);
                pending_.erase(it);
            }
        }

        // --- REJECTION HANDLING ---
        if (status != 200) {
            std::string reason = "unknown";
            if (resp.contains("error") && resp["error"].contains("msg")) {
                reason = resp["error"]["msg"].get<std::string>();
            }

            // Determine client_id from the frame id.
            // Cancel frames use "cancel_<cid>" as id.
            std::string client_id = id;
            if (id.substr(0, 7) == "cancel_") {
                // Cancel rejection — not a new order rejection.
                // Log but don't fire OSM reject (the order may still be live).
                std::cout << "[WS_EXEC] Cancel rejected: id=" << id
                          << " status=" << status << " reason=" << reason << "\n";
                return;
            }

            // New order rejection — fire OSM
            std::cout << "[WS_EXEC] Order rejected: id=" << client_id
                      << " status=" << status << " reason=" << reason << "\n";
            ctx_.osm.on_reject(client_id);
            return;
        }

        // Status 200 = ACK. Fill lifecycle events handled by user stream (BinanceWSUser).
        // We only log here for visibility.
        bool is_cancel = (id.substr(0, 7) == "cancel_");
        if (!is_cancel) {
            // Extract orderId if present for logging
            std::string order_id = "?";
            if (resp.contains("result") && resp["result"].contains("orderId")) {
                order_id = std::to_string(resp["result"]["orderId"].get<int64_t>());
            }
            std::cout << "[WS_EXEC] ACK: id=" << id << " orderId=" << order_id << "\n";
        }

    } catch (const json::exception& e) {
        std::cout << "[WS_EXEC] Parse error: " << e.what() << " msg=" << msg << "\n";
    }
}

// ---------------------------------------------------------------------------
// HMAC-SHA256 signature — delegates to BinanceAuth pattern but inline here
// to avoid constructing a full BinanceAuth object on the WS thread.
// ---------------------------------------------------------------------------
std::string BinanceWSExecution::sign(const std::string& payload) {
    unsigned char digest[32];
    unsigned int  digest_len = 0;

    HMAC(EVP_sha256(),
         api_secret_.c_str(),  static_cast<int>(api_secret_.size()),
         reinterpret_cast<const unsigned char*>(payload.c_str()),
         static_cast<int>(payload.size()),
         digest, &digest_len);

    std::ostringstream out;
    for (unsigned int i = 0; i < digest_len; ++i)
        out << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(digest[i]);
    return out.str();
}

uint64_t BinanceWSExecution::now_us() const {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<microseconds>(
            high_resolution_clock::now().time_since_epoch()
        ).count()
    );
}
