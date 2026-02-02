#include "exchange/binance/BinanceWSMarket.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <unordered_map>
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
// Stream path: combined bookTicker for both symbols.
// bookTicker pushes best bid/ask price + qty on every change — exactly what
// QueuePositionModel::on_book_update() needs.
// Previously subscribed to @aggTrade which has no depth data at all —
// that's why books_ was always empty and estimate() always returned 0.0.
// ---------------------------------------------------------------------------
static constexpr const char* STREAM_PATH =
    "/stream?streams=btcusdt@bookTicker/ethusdt@bookTicker/solusdt@bookTicker";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static uint64_t now_ns() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(
            high_resolution_clock::now().time_since_epoch()
        ).count()
    );
}

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------
BinanceWSMarket::BinanceWSMarket(Context& ctx, const std::string& base)
    : ctx_(ctx), base_(base) {}

void BinanceWSMarket::run(std::atomic<bool>& running) {
    connect_loop(running);
}

void BinanceWSMarket::connect_loop(std::atomic<bool>& running) {
    // SSL context — load system CA certs for peer verification.
    // FIX 1: set_default_verify_paths() loads OS CA bundle portably.
    //         load_verify_locations() is not available on all Boost builds;
    //         set_default_verify_paths() delegates to OpenSSL's default
    //         CA discovery which works on Ubuntu (/etc/ssl/certs/).
    ssl::context ctx{ssl::context::tls_client};
    ctx.set_default_verify_paths();

    while (running.load()) {
        try {
            // Clear sequence tracker on each reconnect — new stream may
            // restart at a lower update ID. Stale entries would silently
            // drop valid updates.
            last_u_.clear();

            asio::io_context ioc;
            tcp::resolver    resolver(ioc);

            // ---------------------------------------------------------------------------
            // Stream host selection: BINANCE_TRADE_MODE=futures → fstream.binance.com
            //                        BINANCE_TRADE_MODE=spot (default) → stream.binance.com
            // ---------------------------------------------------------------------------
            const char* mode_env = std::getenv("BINANCE_TRADE_MODE");
            bool futures_mode = (mode_env && std::string(mode_env) == "futures");
            const char* stream_host = futures_mode ? "fstream.binance.com" : "stream.binance.com";

            // SSL stream wraps TCP. Beast websocket wraps SSL.
            websocket::stream<ssl::stream<tcp::socket>> ws(ioc, ctx);

            auto const results = resolver.resolve(stream_host, "443");
            asio::connect(ws.next_layer().next_layer(), results.begin(), results.end());

            // FIX 2: SSL handshake with proper SNI.
            //         ssl::stream::handshake() takes a handshake_type enum, NOT a hostname.
            //         SNI hostname must be set on the native SSL handle BEFORE the
            //         TLS handshake so the server presents the correct certificate.
            ws.next_layer().set_verify_mode(ssl::verify_peer);
            SSL_set_tlsext_host_name(
                ws.next_layer().native_handle(),
                stream_host
            );
            ws.next_layer().handshake(ssl::stream_base::client);

            // WebSocket handshake
            ws.handshake(stream_host, STREAM_PATH);

            std::cout << "[MARKET_WS] Connected (SSL, bookTicker)\n";

            while (running.load()) {
                // Read with timeout using cancel thread (same pattern as BinanceWSUser)
                static constexpr auto READ_TIMEOUT = std::chrono::milliseconds(500);
                
                beast::flat_buffer buffer;
                beast::error_code ec;
                std::atomic<bool> read_done{false};
                
                std::thread cancel_thread([&]() {
                    std::this_thread::sleep_for(READ_TIMEOUT);
                    if (!read_done.load()) {
                        ws.next_layer().next_layer().cancel();
                    }
                });
                
                ws.read(buffer, ec);
                read_done.store(true);
                cancel_thread.join();
                
                if (ec) {
                    if (ec == boost::asio::error::operation_aborted) {
                        // Timeout - check running flag
                        continue;
                    }
                    throw beast::system_error{ec};
                }
                
                std::string msg = beast::buffers_to_string(buffer.data());
                parse_message(msg);
            }
        } catch (const std::exception& e) {
            if (running.load()) {
                std::cout << "[MARKET_WS] Reconnect (" << e.what() << ")\n";
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }
    }
}

void BinanceWSMarket::parse_message(const std::string& msg) {
    // ---------------------------------------------------------------------------
    // Combined stream envelope:
    //   {"stream":"btcusdt@bookTicker","data":{"u":...,"b":"...","B":"...","a":"...","A":"..."}}
    //
    // bookTicker fields:
    //   u  — last update id
    //   b  — best bid price
    //   B  — best bid quantity
    //   a  — best ask price
    //   A  — best ask quantity
    // 
    // We extract the symbol from the "stream" field (strip @bookTicker suffix)
    // and uppercase it to match QueuePositionModel keys (BTCUSDT / ETHUSDT).
    // ---------------------------------------------------------------------------
    try {
        json j = json::parse(msg);

        // Skip any non-combined frames (subscription confirmations etc)
        if (!j.contains("stream") || !j.contains("data")) return;

        const json& data = j["data"];

        // Must have all four price/qty fields
        if (!data.contains("b") || !data.contains("B") ||
            !data.contains("a") || !data.contains("A")) return;

        // --- Extract symbol from stream name ---
        // "btcusdt@bookTicker" → "btcusdt" → "BTCUSDT"
        std::string stream = j["stream"].get<std::string>();
        size_t at_pos = stream.find('@');
        if (at_pos == std::string::npos) return;
        std::string symbol = stream.substr(0, at_pos);
        for (char& c : symbol) c = static_cast<char>(std::toupper(c));

        // --- Sequence validation ---
        // bookTicker 'u' is monotonically increasing per symbol under normal
        // operation. However, Binance legitimately redelivers updates on
        // reconnect or during bursts — duplicate u is NOT a desync, it's
        // expected. Silently skip duplicates. Only apply strict monotonicity
        // after the first update per symbol per connection.
        if (data.contains("u")) {
            uint64_t u = data["u"].get<uint64_t>();
            auto it = last_u_.find(symbol);

            if (it != last_u_.end() && u <= it->second) {
                // Duplicate or reordered — skip silently. Not a system error.
                return;
            }
            last_u_[symbol] = u;
        }

        // --- Parse prices (Binance sends as strings) ---
        double bid_price = std::stod(data["b"].get<std::string>());
        double bid_depth = std::stod(data["B"].get<std::string>());
        double ask_price = std::stod(data["a"].get<std::string>());
        double ask_depth = std::stod(data["A"].get<std::string>());

        // --- Wire to queue model ---
        uint64_t ts = now_ns();
        ctx_.queue.on_book_update(symbol, bid_price, bid_depth,
                                  ask_price, ask_depth, ts);

        // --- Forensic: record MARKET_TICK ---
        // Closes replay gap: validator can now verify strategy acted on real book.
        ctx_.recorder.write_market(symbol.c_str(), bid_price, bid_depth,
                                   ask_price, ask_depth);

    } catch (const json::parse_error& e) {
        // Malformed frame — skip silently. Binance occasionally sends
        // subscription confirmations or ping/pong that aren't valid JSON events.
        (void)e;
    } catch (const std::exception& e) {
        std::cout << "[MARKET_WS] Parse error: " << e.what() << "\n";
    }
}
