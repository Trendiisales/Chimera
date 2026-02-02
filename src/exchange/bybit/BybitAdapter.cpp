#include "exchange/bybit/BybitAdapter.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <sstream>
#include <iomanip>
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
// Bybit linear perpetuals WebSocket — public channel, no auth for market data.
// Bybit V5 public stream: wss://stream.bybit.com/v5/public/linear
// ---------------------------------------------------------------------------
static constexpr const char* BYBIT_WS_HOST       = "stream.bybit.com";
static constexpr const char* BYBIT_WS_PORT       = "443";
static constexpr const char* BYBIT_WS_PUB_PATH   = "/v5/public/linear";
static constexpr const char* BYBIT_WS_PRIV_PATH  = "/v5/private";

// Bybit linear perpetuals use the same symbol format as Binance (BTCUSDT).
// No mapping needed.

// ---------------------------------------------------------------------------
// Construction: load credentials from env.
// ---------------------------------------------------------------------------
BybitAdapter::BybitAdapter(Context& ctx, const std::string& rest, const std::string& ws)
    : ctx_(ctx), rest_base_(rest), ws_base_(ws) {

    const char* key    = std::getenv("BYBIT_API_KEY");
    const char* secret = std::getenv("BYBIT_API_SECRET");

    if (key && secret) {
        auth_ = std::make_unique<BybitAuth>(key, secret);
        rest_ = std::make_unique<BybitRestClient>(rest_base_, *auth_);
        has_credentials_ = true;
        std::cout << "[BYBIT] Credentials loaded (key=" << std::string(key, 8) << "...)\n";
    } else {
        has_credentials_ = false;
        std::cout << "[BYBIT] No credentials — market data only\n";
    }
}

std::string BybitAdapter::name() const { return "BYBIT"; }

// ---------------------------------------------------------------------------
// Market WebSocket: public ticker subscription for linear perpetuals.
// Bybit V5 subscription: {"op":"subscribe","args":["linear.tickers.BTCUSDT",...]}
// Push frame: {"topic":"linear.tickers.BTCUSDT","data":{"symbol":"BTCUSDT",
//   "bidPrice":"...","bidSize":"...","askPrice":"...","askSize":"...",...}}
// ---------------------------------------------------------------------------
void BybitAdapter::run_market(std::atomic<bool>& running) {
    market_connect_loop(running);
}

void BybitAdapter::market_connect_loop(std::atomic<bool>& running) {
    ssl::context sslctx{ssl::context::tls_client};
    sslctx.set_default_verify_paths();

    json sub = {
        {"op", "subscribe"},
        {"args", json::array({
            "linear.tickers.BTCUSDT",
            "linear.tickers.ETHUSDT",
            "linear.tickers.SOLUSDT"
        })}
    };
    std::string sub_payload = sub.dump();

    while (running.load()) {
        try {
            asio::io_context ioc;
            tcp::resolver    resolver(ioc);
            websocket::stream<ssl::stream<tcp::socket>> ws(ioc, sslctx);

            auto results = resolver.resolve(BYBIT_WS_HOST, BYBIT_WS_PORT);
            asio::connect(ws.next_layer().next_layer(), results.begin(), results.end());

            ws.next_layer().set_verify_mode(ssl::verify_peer);
            SSL_set_tlsext_host_name(
                ws.next_layer().native_handle(), BYBIT_WS_HOST);
            ws.next_layer().handshake(ssl::stream_base::client);
            ws.handshake(BYBIT_WS_HOST, BYBIT_WS_PUB_PATH);

            ws.write(asio::const_buffer(sub_payload.data(), sub_payload.size()));
            std::cout << "[BYBIT] Market WS connected, subscribed to tickers\n";

            while (running.load()) {
                beast::flat_buffer buffer;
                ws.read(buffer);
                parse_ticker(beast::buffers_to_string(buffer.data()));
            }
        } catch (const std::exception& e) {
            if (running.load()) {
                std::cout << "[BYBIT] Market WS reconnect (" << e.what() << ")\n";
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Parse Bybit ticker push:
//   {"topic":"linear.tickers.BTCUSDT",
//    "data":{"symbol":"BTCUSDT","bidPrice":"...","bidSize":"...",
//            "askPrice":"...","askSize":"...",...}}
// ---------------------------------------------------------------------------
void BybitAdapter::parse_ticker(const std::string& msg) {
    try {
        json j = json::parse(msg);

        // Only process topic-based pushes (not subscription confirmations)
        if (!j.contains("topic") || !j.contains("data")) return;

        std::string topic = j["topic"].get<std::string>();
        // Must be a ticker topic
        if (topic.find("linear.tickers.") != 0) return;

        const json& data = j["data"];
        if (!data.contains("symbol") || !data.contains("bidPrice") ||
            !data.contains("bidSize") || !data.contains("askPrice") ||
            !data.contains("askSize")) return;

        std::string symbol   = data["symbol"].get<std::string>();
        double bid_price     = std::stod(data["bidPrice"].get<std::string>());
        double bid_depth     = std::stod(data["bidSize"].get<std::string>());
        double ask_price     = std::stod(data["askPrice"].get<std::string>());
        double ask_depth     = std::stod(data["askSize"].get<std::string>());

        // Reject zero prices (no liquidity)
        if (bid_price <= 0.0 || ask_price <= 0.0) return;

        using namespace std::chrono;
        uint64_t ts_ns = static_cast<uint64_t>(
            duration_cast<nanoseconds>(
                high_resolution_clock::now().time_since_epoch()).count());

        ctx_.queue.on_book_update(symbol, bid_price, bid_depth,
                                  ask_price, ask_depth, ts_ns);
        ctx_.recorder.write_market(symbol.c_str(), bid_price, bid_depth,
                                   ask_price, ask_depth);

    } catch (const json::parse_error&) {
        // Subscription confirmations — skip
    } catch (const std::exception& e) {
        std::cout << "[BYBIT] Ticker parse: " << e.what() << "\n";
    }
}

// ---------------------------------------------------------------------------
// User stream: private WebSocket with API key auth.
// Bybit V5 private stream auth: send {"op":"auth","args":[apiKey, signature]}
// where signature = HMAC-SHA256(secret, apiKey + recvWindow + timestamp)
// Then subscribe to order/execution channels.
// No-op heartbeat if no credentials.
// ---------------------------------------------------------------------------
void BybitAdapter::run_user(std::atomic<bool>& running) {
    if (!has_credentials_) {
        while (running.load())
            std::this_thread::sleep_for(std::chrono::seconds(30));
        return;
    }

    ssl::context sslctx{ssl::context::tls_client};
    sslctx.set_default_verify_paths();

    while (running.load()) {
        try {
            asio::io_context ioc;
            tcp::resolver    resolver(ioc);
            websocket::stream<ssl::stream<tcp::socket>> ws(ioc, sslctx);

            auto results = resolver.resolve(BYBIT_WS_HOST, BYBIT_WS_PORT);
            asio::connect(ws.next_layer().next_layer(), results.begin(), results.end());

            ws.next_layer().set_verify_mode(ssl::verify_peer);
            SSL_set_tlsext_host_name(
                ws.next_layer().native_handle(), BYBIT_WS_HOST);
            ws.next_layer().handshake(ssl::stream_base::client);
            ws.handshake(BYBIT_WS_HOST, BYBIT_WS_PRIV_PATH);

            // ---------------------------------------------------------------------------
            // Auth: Bybit private WS uses "auth" op.
            // Signature payload for auth: apiKey + recvWindow + timestamp (empty body)
            // ---------------------------------------------------------------------------
            std::string timestamp  = BybitAuth::now_ms();
            std::string recv_window = "5000";
            // Bybit auth signature: sign(timestamp, empty_payload, recv_window)
            // pre_sign = apiKey + recvWindow + timestamp + payload(empty)
            std::string signature = auth_->sign(timestamp, "", recv_window);

            json auth_msg = {
                {"op", "auth"},
                {"args", json::array({
                    auth_->api_key(),
                    signature
                })},
                {"expiresIn", 900}
            };
            std::string auth_str = auth_msg.dump();
            ws.write(asio::const_buffer(auth_str.data(), auth_str.size()));

            // Read auth response
            {
                beast::flat_buffer buf;
                ws.read(buf);
                std::string resp = beast::buffers_to_string(buf.data());
                json r = json::parse(resp);
                // Bybit returns {"op":"auth","success":true}
                if (!r.contains("success") || !r["success"].get<bool>()) {
                    std::cout << "[BYBIT] User WS auth failed: " << resp << "\n";
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    continue;
                }
            }

            // Subscribe to order + execution channels (linear category)
            json sub = {
                {"op", "subscribe"},
                {"args", json::array({
                    "order.linear",
                    "execution.linear"
                })}
            };
            std::string sub_str = sub.dump();
            ws.write(asio::const_buffer(sub_str.data(), sub_str.size()));
            std::cout << "[BYBIT] User WS authenticated, subscribed\n";

            while (running.load()) {
                beast::flat_buffer buffer;
                ws.read(buffer);
                std::string msg = beast::buffers_to_string(buffer.data());

                try {
                    json j = json::parse(msg);

                    // Only process topic-based pushes
                    if (!j.contains("topic") || !j.contains("data")) continue;
                    std::string topic = j["topic"].get<std::string>();
                    const json& data_arr = j["data"];
                    if (!data_arr.is_array()) continue;

                    if (topic == "order.linear") {
                        // Order status updates
                        for (const auto& ev : data_arr) {
                            std::string symbol   = ev.contains("symbol")   ? ev["symbol"].get<std::string>()   : "";
                            std::string orderId  = ev.contains("orderId")  ? ev["orderId"].get<std::string>()  : "";
                            std::string clientId = ev.contains("clientOID") ? ev["clientOID"].get<std::string>() : "";
                            std::string status   = ev.contains("orderStatus") ? ev["orderStatus"].get<std::string>() : "";

                            if (status == "New" || status == "PartiallyFilled") {
                                if (!clientId.empty() && !orderId.empty()) {
                                    ctx_.osm.on_ack(clientId, orderId);
                                    ctx_.recorder.write_ack(clientId, orderId);
                                    std::cout << "[BYBIT] ACK: " << symbol
                                              << " client=" << clientId << " exch=" << orderId << "\n";
                                }
                            } else if (status == "Cancelled" || status == "Deactivated") {
                                if (!orderId.empty()) {
                                    ctx_.osm.on_cancel(orderId);
                                    ctx_.recorder.write_cancel(clientId);
                                    std::cout << "[BYBIT] CANCEL: " << symbol
                                              << " client=" << clientId << "\n";
                                }
                            } else if (status == "Filled") {
                                if (!orderId.empty() && ev.contains("cumExecQty")) {
                                    double qty = std::stod(ev["cumExecQty"].get<std::string>());
                                    double px  = ev.contains("avgPrice")
                                                 ? std::stod(ev["avgPrice"].get<std::string>()) : 0.0;
                                    if (qty > 0.0) {
                                        ctx_.osm.on_fill(orderId, qty);
                                        ctx_.recorder.write_fill(clientId, qty, px);
                                        std::cout << "[BYBIT] FILL: " << symbol
                                                  << " qty=" << qty << " px=" << px << "\n";
                                    }
                                }
                            } else if (status == "Rejected") {
                                if (!clientId.empty()) {
                                    ctx_.osm.on_reject(clientId);
                                    ctx_.recorder.write_reject(clientId);
                                    std::cout << "[BYBIT] REJECT: " << symbol
                                              << " client=" << clientId << "\n";
                                }
                            }
                        }
                    } else if (topic == "execution.linear") {
                        // Incremental fill notifications
                        for (const auto& ev : data_arr) {
                            std::string symbol   = ev.contains("symbol")   ? ev["symbol"].get<std::string>()   : "";
                            std::string orderId  = ev.contains("orderId")  ? ev["orderId"].get<std::string>()  : "";
                            std::string clientId = ev.contains("clientOID") ? ev["clientOID"].get<std::string>() : "";

                            if (!orderId.empty() && ev.contains("execQty")) {
                                double qty = std::stod(ev["execQty"].get<std::string>());
                                double px  = ev.contains("execPrice")
                                             ? std::stod(ev["execPrice"].get<std::string>()) : 0.0;
                                if (qty > 0.0) {
                                    ctx_.osm.on_fill(orderId, qty);
                                    ctx_.recorder.write_fill(clientId, qty, px);
                                    ctx_.risk.on_execution_ack(symbol, qty);
                                    std::cout << "[BYBIT] FILL(inc): " << symbol
                                              << " qty=" << qty << " px=" << px << "\n";
                                }
                            }
                        }
                    }
                } catch (...) { /* sub confirmations / pongs */ }
            }

        } catch (const std::exception& e) {
            if (running.load()) {
                std::cout << "[BYBIT] User WS reconnect (" << e.what() << ")\n";
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Execution — only reachable if LiveArmSystem armed+verified.
// Bybit V5 place order: POST /v5/order/submit
// ---------------------------------------------------------------------------
bool BybitAdapter::send_order(const VenueOrder& ord) {
    if (!has_credentials_) {
        std::cout << "[BYBIT] send_order BLOCKED — no credentials\n";
        return false;
    }

    std::string side = (ord.qty > 0.0) ? "Buy" : "Sell";
    double abs_qty   = std::fabs(ord.qty);

    std::ostringstream body;
    body << "{"
         << "\"category\":\"linear\","
         << "\"symbol\":\""    << ord.symbol    << "\","
         << "\"side\":\""      << side          << "\","
         << "\"orderType\":\"Limit\","
         << "\"qty\":\""       << std::fixed << std::setprecision(8) << abs_qty  << "\","
         << "\"price\":\""     << std::fixed << std::setprecision(8) << ord.price << "\","
         << "\"clientOID\":\"" << ord.client_id << "\","
         << "\"timeInForce\":\"GTC\""
         << "}";

    try {
        std::string resp = rest_->place_order(body.str());
        std::cout << "[BYBIT] Order sent: " << resp << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cout << "[BYBIT] Order failed: " << e.what() << "\n";
        return false;
    }
}

bool BybitAdapter::cancel_order(const std::string& client_id) {
    if (!has_credentials_) {
        std::cout << "[BYBIT] cancel_order BLOCKED — no credentials\n";
        return false;
    }

    // Bybit cancel by clientOID — try across all traded symbols
    std::vector<std::string> symbols = {"BTCUSDT", "ETHUSDT", "SOLUSDT"};

    for (const auto& symbol : symbols) {
        std::ostringstream body;
        body << "{"
             << "\"category\":\"linear\","
             << "\"symbol\":\""    << symbol     << "\","
             << "\"clientOID\":\"" << client_id  << "\""
             << "}";
        try {
            std::string resp = rest_->cancel_order(body.str());
            json r = json::parse(resp);
            // Bybit returns {"ret_code":"0"} on success
            if (r.contains("ret_code") && r["ret_code"].get<std::string>() == "0") {
                std::cout << "[BYBIT] Cancel sent: " << resp << "\n";
                return true;
            }
        } catch (...) { /* try next */ }
    }

    std::cout << "[BYBIT] Cancel failed for client_id=" << client_id << "\n";
    return false;
}

// ---------------------------------------------------------------------------
// Cold-start reconciliation: real REST if credentials present, empty if not.
// ---------------------------------------------------------------------------
bool BybitAdapter::get_all_positions(std::vector<VenuePosition>& out) {
    out.clear();
    if (!has_credentials_) return true;

    try {
        std::string raw = rest_->get_positions();
        json j = json::parse(raw);

        // Bybit V5 envelope: {"ret_code":"0","result":{"list":[...]}}
        if (!j.contains("result") || !j["result"].contains("list")) return true;

        const json& list = j["result"]["list"];
        if (!list.is_array()) return true;

        for (const auto& pos : list) {
            if (!pos.contains("symbol") || !pos.contains("size")) continue;

            std::string symbol  = pos["symbol"].get<std::string>();
            double qty          = std::stod(pos["size"].get<std::string>());
            double entry_px     = pos.contains("avgPrice")
                                  ? std::stod(pos["avgPrice"].get<std::string>()) : 0.0;

            // Bybit uses "side" field: "Buy"=long, "Sell"=short
            // Negate qty for short positions
            if (pos.contains("side") && pos["side"].get<std::string>() == "Sell")
                qty = -qty;

            if (std::fabs(qty) > 1e-8)
                out.push_back({symbol, qty, entry_px});
        }
        std::cout << "[BYBIT] Reconcile: " << out.size() << " open positions\n";
        return true;
    } catch (const std::exception& e) {
        std::cout << "[BYBIT] Position fetch failed: " << e.what() << "\n";
        return false;
    }
}

bool BybitAdapter::get_all_open_orders(std::vector<VenueOpenOrder>& out) {
    out.clear();
    if (!has_credentials_) return true;

    try {
        std::string raw = rest_->get_open_orders();
        json j = json::parse(raw);

        if (!j.contains("result") || !j["result"].contains("list")) return true;

        const json& list = j["result"]["list"];
        if (!list.is_array()) return true;

        for (const auto& ord : list) {
            if (!ord.contains("symbol")) continue;

            std::string symbol    = ord["symbol"].get<std::string>();
            std::string client_id = ord.contains("clientOID") ? ord["clientOID"].get<std::string>() : "";
            double price          = ord.contains("price") ? std::stod(ord["price"].get<std::string>())  : 0.0;
            double qty            = ord.contains("qty")   ? std::stod(ord["qty"].get<std::string>())    : 0.0;

            out.push_back({client_id, symbol, price, qty});
        }
        std::cout << "[BYBIT] Reconcile: " << out.size() << " open orders\n";
        return true;
    } catch (const std::exception& e) {
        std::cout << "[BYBIT] Open orders fetch failed: " << e.what() << "\n";
        return false;
    }
}
