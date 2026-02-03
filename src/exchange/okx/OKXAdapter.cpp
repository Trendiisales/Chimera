#include "exchange/okx/OKXAdapter.hpp"
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
// OKX perpetual swap WebSocket — public channel, no auth for market data.
// ---------------------------------------------------------------------------
static constexpr const char* OKX_WS_HOST = "ws.okx.com";
static constexpr const char* OKX_WS_PORT = "8443";
static constexpr const char* OKX_WS_PATH = "/ws/v5/public";

// ---------------------------------------------------------------------------
// Symbol mapping: internal <-> OKX perpetual swap instId
//   BTCUSDT  <->  BTC-USDT-SWAP
//   ETHUSDT  <->  ETH-USDT-SWAP
//   SOLUSDT  <->  SOL-USDT-SWAP
// Generic fallback: XXXUSDT -> XXX-USDT-SWAP
// ---------------------------------------------------------------------------
std::string OKXAdapter::to_okx_symbol(const std::string& internal_sym) {
    if (internal_sym == "BTCUSDT") return "BTC-USDT-SWAP";
    if (internal_sym == "ETHUSDT") return "ETH-USDT-SWAP";
    if (internal_sym == "SOLUSDT") return "SOL-USDT-SWAP";
    if (internal_sym.size() > 4 &&
        internal_sym.substr(internal_sym.size() - 4) == "USDT") {
        return internal_sym.substr(0, internal_sym.size() - 4) + "-USDT-SWAP";
    }
    return internal_sym;
}

std::string OKXAdapter::from_okx_symbol(const std::string& okx_sym) {
    if (okx_sym == "BTC-USDT-SWAP") return "BTCUSDT";
    if (okx_sym == "ETH-USDT-SWAP") return "ETHUSDT";
    if (okx_sym == "SOL-USDT-SWAP") return "SOLUSDT";
    size_t d1 = okx_sym.find('-');
    if (d1 == std::string::npos) return okx_sym;
    std::string base = okx_sym.substr(0, d1);
    size_t d2 = okx_sym.find('-', d1 + 1);
    if (d2 != std::string::npos) {
        std::string quote = okx_sym.substr(d1 + 1, d2 - d1 - 1);
        return base + quote;
    }
    return base + okx_sym.substr(d1 + 1);
}

// ---------------------------------------------------------------------------
// Construction: load credentials from env, build auth + REST.
// Market data is public — adapter starts even without credentials.
// ---------------------------------------------------------------------------
OKXAdapter::OKXAdapter(Context& ctx, const std::string& rest, const std::string& ws)
    : ctx_(ctx), rest_base_(rest), ws_base_(ws) {

    const char* key        = std::getenv("OKX_API_KEY");
    const char* secret     = std::getenv("OKX_API_SECRET");
    const char* passphrase = std::getenv("OKX_PASSPHRASE");

    if (key && secret && passphrase) {
        auth_ = std::make_unique<OKXAuth>(key, secret, passphrase);
        rest_ = std::make_unique<OKXRestClient>(rest_base_, *auth_);
        has_credentials_ = true;
        std::cout << "[OKX] Credentials loaded (key=" << std::string(key, 8) << "...)\n";
    } else {
        has_credentials_ = false;
        std::cout << "[OKX] No credentials — market data only\n";
    }
}

std::string OKXAdapter::name() const { return "OKX"; }

// ---------------------------------------------------------------------------
// Market WebSocket: public tickers for all 3 swap pairs.
// Reconnect loop with 2s backoff.
// ---------------------------------------------------------------------------
void OKXAdapter::run_market(std::atomic<bool>& running) {
    market_connect_loop(running);
}

void OKXAdapter::market_connect_loop(std::atomic<bool>& running) {
    ssl::context sslctx{ssl::context::tls_client};
    sslctx.set_default_verify_paths();

    json sub = {
        {"op", "subscribe"},
        {"args", json::array({
            json{{"channel", "tickers"}, {"instId", "BTC-USDT-SWAP"}},
            json{{"channel", "tickers"}, {"instId", "ETH-USDT-SWAP"}},
            json{{"channel", "tickers"}, {"instId", "SOL-USDT-SWAP"}}
        })}
    };
    std::string sub_payload = sub.dump();

    while (running.load()) {
        try {
            asio::io_context ioc;
            tcp::resolver    resolver(ioc);
            websocket::stream<ssl::stream<tcp::socket>> ws(ioc, sslctx);

            auto results = resolver.resolve(OKX_WS_HOST, OKX_WS_PORT);
            asio::connect(ws.next_layer().next_layer(), results.begin(), results.end());

            ws.next_layer().set_verify_mode(ssl::verify_peer);
            SSL_set_tlsext_host_name(
                ws.next_layer().native_handle(), OKX_WS_HOST);
            ws.next_layer().handshake(ssl::stream_base::client);
            ws.handshake(OKX_WS_HOST, OKX_WS_PATH);

            ws.write(asio::const_buffer(sub_payload.data(), sub_payload.size()));
            std::cout << "[OKX] Market WS connected, subscribed to tickers\n";

            while (running.load()) {
                beast::flat_buffer buffer;
                ws.read(buffer);
                parse_ticker(beast::buffers_to_string(buffer.data()));
            }
        } catch (const std::exception& e) {
            if (running.load()) {
                std::cout << "[OKX] Market WS reconnect (" << e.what() << ")\n";
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Parse OKX ticker push:
//   {"arg":{"channel":"tickers","instId":"BTC-USDT-SWAP"},
//    "data":[{"instId":"...","bidPx":"...","bidSz":"...","askPx":"...","askSz":"..."}]}
// ---------------------------------------------------------------------------
void OKXAdapter::parse_ticker(const std::string& msg) {
    try {
        json j = json::parse(msg);
        if (!j.contains("arg") || !j.contains("data")) return;
        if (!j["arg"].contains("channel") ||
            j["arg"]["channel"].get<std::string>() != "tickers") return;

        const json& data_arr = j["data"];
        if (!data_arr.is_array() || data_arr.empty()) return;

        const json& tick = data_arr[0];
        if (!tick.contains("instId") || !tick.contains("bidPx") ||
            !tick.contains("bidSz") || !tick.contains("askPx") ||
            !tick.contains("askSz")) return;

        std::string symbol   = from_okx_symbol(tick["instId"].get<std::string>());
        double bid_price     = std::stod(tick["bidPx"].get<std::string>());
        double bid_depth     = std::stod(tick["bidSz"].get<std::string>());
        double ask_price     = std::stod(tick["askPx"].get<std::string>());
        double ask_depth     = std::stod(tick["askSz"].get<std::string>());

        // OKX sends "0" when no liquidity — reject
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
        // Subscription confirmations / pongs — skip
    } catch (const std::exception& e) {
        std::cout << "[OKX] Ticker parse: " << e.what() << "\n";
    }
}

// ---------------------------------------------------------------------------
// User stream: private WebSocket with login + fills/orders subscription.
// No-op heartbeat if credentials are missing.
// ---------------------------------------------------------------------------
void OKXAdapter::run_user(std::atomic<bool>& running) {
    if (!has_credentials_) {
        while (running.load())
            std::this_thread::sleep_for(std::chrono::seconds(30));
        return;
    }

    ssl::context sslctx{ssl::context::tls_client};
    sslctx.set_default_verify_paths();

    static constexpr const char* PRIV_PATH = "/ws/v5/private";

    while (running.load()) {
        try {
            asio::io_context ioc;
            tcp::resolver    resolver(ioc);
            websocket::stream<ssl::stream<tcp::socket>> ws(ioc, sslctx);

            auto results = resolver.resolve(OKX_WS_HOST, OKX_WS_PORT);
            asio::connect(ws.next_layer().next_layer(), results.begin(), results.end());

            ws.next_layer().set_verify_mode(ssl::verify_peer);
            SSL_set_tlsext_host_name(
                ws.next_layer().native_handle(), OKX_WS_HOST);
            ws.next_layer().handshake(ssl::stream_base::client);
            ws.handshake(OKX_WS_HOST, PRIV_PATH);

            // ---------------------------------------------------------------------------
            // Login: HMAC-SHA256(secret, timestamp + "GET" + "/users/self/verify") -> base64
            // ---------------------------------------------------------------------------
            std::string timestamp = OKXAuth::now_sec();
            std::string signature = auth_->sign(timestamp, "GET", "/users/self/verify");

            json login = {
                {"op", "login"},
                {"args", json::array({json{
                    {"apiKey",     auth_->api_key()},
                    {"passphrase", auth_->passphrase()},
                    {"timestamp",  timestamp},
                    {"sign",       signature}
                }})}
            };
            std::string login_str = login.dump();
            ws.write(asio::const_buffer(login_str.data(), login_str.size()));

            // Read login response
            {
                beast::flat_buffer buf;
                ws.read(buf);
                std::string resp = beast::buffers_to_string(buf.data());
                json r = json::parse(resp);
                if (!r.contains("event") || r["event"].get<std::string>() != "login" ||
                    !r.contains("code") || r["code"].get<std::string>() != "0") {
                    std::cout << "[OKX] User WS login failed: " << resp << "\n";
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    continue;
                }
            }

            // Subscribe to fills + orders
            json sub = {
                {"op", "subscribe"},
                {"args", json::array({
                    json{{"channel", "fills"},  {"instType", "SWAP"}},
                    json{{"channel", "orders"}, {"instType", "SWAP"}}
                })}
            };
            std::string sub_str = sub.dump();
            ws.write(asio::const_buffer(sub_str.data(), sub_str.size()));
            std::cout << "[OKX] User WS logged in, subscribed\n";

            while (running.load()) {
                beast::flat_buffer buffer;
                ws.read(buffer);
                std::string msg = beast::buffers_to_string(buffer.data());

                try {
                    json j = json::parse(msg);
                    if (!j.contains("arg") || !j.contains("data")) continue;
                    std::string channel = j["arg"]["channel"].get<std::string>();
                    const json& data_arr = j["data"];
                    if (!data_arr.is_array()) continue;

                    for (const auto& ev : data_arr) {
                        std::string instId  = ev.contains("instId")  ? ev["instId"].get<std::string>()  : "";
                        std::string clOrdId = ev.contains("clOrdId") ? ev["clOrdId"].get<std::string>() : "";
                        std::string ordId   = ev.contains("ordId")   ? ev["ordId"].get<std::string>()   : "";
                        std::string symbol  = from_okx_symbol(instId);

                        if (channel == "orders") {
                            std::string state = ev.contains("state") ? ev["state"].get<std::string>() : "";

                            if (state == "live" || state == "partially_filled") {
                                if (!clOrdId.empty() && !ordId.empty()) {
                                    ctx_.osm.on_ack(clOrdId, ordId);
                                    ctx_.recorder.write_ack(clOrdId, ordId);
                                    std::cout << "[OKX] ACK: " << symbol
                                              << " client=" << clOrdId << " exch=" << ordId << "\n";
                                }
                            } else if (state == "canceled" || state == "expired") {
                                if (!ordId.empty()) {
                                    ctx_.osm.on_cancel(ordId);
                                    ctx_.recorder.write_cancel(clOrdId);
                                    std::cout << "[OKX] CANCEL: " << symbol
                                              << " client=" << clOrdId << "\n";
                                }
                            } else if (state == "filled") {
                                if (!ordId.empty() && ev.contains("accFillSz")) {
                                    double qty = std::stod(ev["accFillSz"].get<std::string>());
                                    double px  = ev.contains("avgPx") ? std::stod(ev["avgPx"].get<std::string>()) : 0.0;
                                    if (qty > 0.0) {
                                        ctx_.osm.on_fill(ordId, qty);
                                        ctx_.recorder.write_fill(clOrdId, qty, px);
                                        std::cout << "[OKX] FILL: " << symbol
                                                  << " qty=" << qty << " px=" << px << "\n";
                                    }
                                }
                            }
                        } else if (channel == "fills") {
                            if (!ordId.empty() && ev.contains("sz")) {
                                double qty = std::stod(ev["sz"].get<std::string>());
                                double px  = ev.contains("px") ? std::stod(ev["px"].get<std::string>()) : 0.0;
                                if (qty > 0.0) {
                                    ctx_.osm.on_fill(ordId, qty);
                                    ctx_.recorder.write_fill(clOrdId, qty, px);
                                    ctx_.risk.on_execution_ack(symbol, qty);
                                    std::cout << "[OKX] FILL(inc): " << symbol
                                              << " qty=" << qty << " px=" << px << "\n";
                                }
                            }
                        }
                    }
                } catch (...) { /* sub confirmations / pongs */ }
            }

        } catch (const std::exception& e) {
            if (running.load()) {
                std::cout << "[OKX] User WS reconnect (" << e.what() << ")\n";
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Execution — only reachable if LiveArmSystem armed+verified.
// Shadow mode never reaches here (ExecutionRouter gates it).
// ---------------------------------------------------------------------------
bool OKXAdapter::send_order(const VenueOrder& ord) {
    if (!has_credentials_) {
        std::cout << "[OKX] send_order BLOCKED — no credentials\n";
        return false;
    }

    std::string okx_sym = to_okx_symbol(ord.symbol);
    std::string side    = (ord.qty > 0.0) ? "buy" : "sell";
    double abs_qty      = std::fabs(ord.qty);

    std::ostringstream body;
    body << "{"
         << "\"instId\":\""   << okx_sym       << "\","
         << "\"tdMode\":\"cross\","
         << "\"side\":\""     << side          << "\","
         << "\"ordType\":\"limit\","
         << "\"clOrdId\":\""  << ord.client_id << "\","
         << "\"sz\":\""       << std::fixed << std::setprecision(8) << abs_qty  << "\","
         << "\"px\":\""       << std::fixed << std::setprecision(8) << ord.price << "\""
         << "}";

    try {
        std::string resp = rest_->place_order(body.str());
        std::cout << "[OKX] Order sent: " << resp << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cout << "[OKX] Order failed: " << e.what() << "\n";
        return false;
    }
}

bool OKXAdapter::cancel_order(const std::string& client_id) {
    if (!has_credentials_) {
        std::cout << "[OKX] cancel_order BLOCKED — no credentials\n";
        return false;
    }

    // OKX cancel requires instId. Try all traded pairs until one succeeds.
    std::vector<std::string> instIds = {"BTC-USDT-SWAP", "ETH-USDT-SWAP", "SOL-USDT-SWAP"};

    for (const auto& instId : instIds) {
        std::ostringstream body;
        body << "{"
             << "\"instId\":\""  << instId     << "\","
             << "\"clOrdId\":\"" << client_id  << "\""
             << "}";
        try {
            std::string resp = rest_->cancel_order(body.str());
            json r = json::parse(resp);
            if (r.contains("code") && r["code"].get<std::string>() == "0") {
                std::cout << "[OKX] Cancel sent: " << resp << "\n";
                return true;
            }
        } catch (...) { /* try next */ }
    }

    std::cout << "[OKX] Cancel failed for client_id=" << client_id << "\n";
    return false;
}

// ---------------------------------------------------------------------------
// Cold-start reconciliation: real REST queries if credentials present,
// empty (shadow-safe) if not.
// ---------------------------------------------------------------------------
bool OKXAdapter::get_all_positions(std::vector<VenuePosition>& out) {
    out.clear();
    if (!has_credentials_) return true;

    try {
        std::string raw = rest_->get_positions();
        json j = json::parse(raw);
        if (!j.contains("data") || !j["data"].is_array()) return true;

        for (const auto& pos : j["data"]) {
            if (!pos.contains("instId") || !pos.contains("pos")) continue;
            std::string symbol  = from_okx_symbol(pos["instId"].get<std::string>());
            double qty          = std::stod(pos["pos"].get<std::string>());
            double entry_px     = pos.contains("avgPx")
                                  ? std::stod(pos["avgPx"].get<std::string>()) : 0.0;
            if (std::fabs(qty) > 1e-8)
                out.push_back({symbol, qty, entry_px});
        }
        std::cout << "[OKX] Reconcile: " << out.size() << " open positions\n";
        return true;
    } catch (const std::exception& e) {
        std::cout << "[OKX] Position fetch failed: " << e.what() << "\n";
        return false;
    }
}

bool OKXAdapter::get_all_open_orders(std::vector<VenueOpenOrder>& out) {
    out.clear();
    if (!has_credentials_) return true;

    try {
        std::string raw = rest_->get_open_orders();
        json j = json::parse(raw);
        if (!j.contains("data") || !j["data"].is_array()) return true;

        for (const auto& ord : j["data"]) {
            if (!ord.contains("instId") || !ord.contains("clOrdId")) continue;
            std::string symbol    = from_okx_symbol(ord["instId"].get<std::string>());
            std::string client_id = ord["clOrdId"].get<std::string>();
            double price          = ord.contains("px") ? std::stod(ord["px"].get<std::string>())  : 0.0;
            double qty            = ord.contains("sz") ? std::stod(ord["sz"].get<std::string>())  : 0.0;
            out.push_back({client_id, symbol, price, qty});
        }
        std::cout << "[OKX] Reconcile: " << out.size() << " open orders\n";
        return true;
    } catch (const std::exception& e) {
        std::cout << "[OKX] Open orders fetch failed: " << e.what() << "\n";
        return false;
    }
}
