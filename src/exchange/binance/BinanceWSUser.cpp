#include "exchange/binance/BinanceWSUser.hpp"
#include "exchange/binance/BinanceAuth.hpp"
#include "exchange/binance/BinanceRestClient.hpp"
#include "forensics/EdgeAttribution.hpp"
#include "control/DeskArbiter.hpp"
#include <iostream>
#include <thread>
#include <chrono>
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
// Listen key lifetime on Binance USDT-M: 60 minutes.
// Keepalive must be sent before expiry. We target 30-minute intervals
// (half the lifetime) to have comfortable margin.
// ---------------------------------------------------------------------------
static constexpr int64_t KEEPALIVE_INTERVAL_MS = 30 * 60 * 1000; // 30 min

// ---------------------------------------------------------------------------
BinanceWSUser::BinanceWSUser(Context& ctx, const std::string& rest_base)
    : ctx_(ctx), rest_base_(rest_base) {}

void BinanceWSUser::run(std::atomic<bool>& running) {
    connect_loop(running);
}

void BinanceWSUser::connect_loop(std::atomic<bool>& running) {
    ssl::context ctx{ssl::context::tls_client};
    ctx.set_default_verify_paths();

    while (running.load()) {
        try {
            // --- Credentials ---
            const char* key    = std::getenv("BINANCE_API_KEY");
            const char* secret = std::getenv("BINANCE_API_SECRET");
            if (!key || !secret)
                throw std::runtime_error("Missing BINANCE_API_KEY/SECRET");

            BinanceAuth        auth(key, secret);
            BinanceRestClient  rest(rest_base_, auth);

            // ---------------------------------------------------------------------------
            // create_listen_key() returns raw JSON: {"listenKey":"abc123..."}
            // On auth failure Binance returns {"code":-2015,"msg":"Invalid API-key..."}.
            // Check for error response before extracting listenKey — otherwise
            // we crash with type_error.302 on null listenKey field.
            // ---------------------------------------------------------------------------
            std::string raw_response = rest.create_listen_key();
            json listen_key_json = json::parse(raw_response);

            if (listen_key_json.contains("code")) {
                // Binance error response
                std::string msg = listen_key_json.contains("msg")
                    ? listen_key_json["msg"].get<std::string>()
                    : "unknown error";
                throw std::runtime_error("Listen key creation failed: " + msg +
                    " (code=" + std::to_string(listen_key_json["code"].get<int>()) + ")");
            }

            if (!listen_key_json.contains("listenKey") || listen_key_json["listenKey"].is_null()) {
                throw std::runtime_error("Listen key response missing listenKey field");
            }

            std::string listen_key = listen_key_json["listenKey"].get<std::string>();
            std::string endpoint   = "/ws/" + listen_key;

            std::cout << "[USER_WS] Listen key created, connecting...\n";

            // ---------------------------------------------------------------------------
            // Stream host selection: BINANCE_TRADE_MODE=futures → fstream.binance.com
            //                        BINANCE_TRADE_MODE=spot (default) → stream.binance.com
            // ---------------------------------------------------------------------------
            const char* mode_env = std::getenv("BINANCE_TRADE_MODE");
            bool futures_mode = (mode_env && std::string(mode_env) == "futures");
            const char* stream_host = futures_mode ? "fstream.binance.com" : "stream.binance.com";

            // --- Connect ---
            asio::io_context ioc;
            tcp::resolver    resolver(ioc);

            websocket::stream<ssl::stream<tcp::socket>> ws(ioc, ctx);

            auto const results = resolver.resolve(stream_host, "443");
            asio::connect(ws.next_layer().next_layer(), results.begin(), results.end());

            ws.next_layer().set_verify_mode(ssl::verify_peer);
            SSL_set_tlsext_host_name(
                ws.next_layer().native_handle(),
                stream_host
            );
            ws.next_layer().handshake(ssl::stream_base::client);

            ws.handshake(stream_host, endpoint);

            std::cout << "[USER_WS] Connected (SSL)\n";

            ctx_.ws_user_alive.store(true);
            ctx_.needs_reconcile.store(true);

            int64_t last_keepalive_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();

            // ---------------------------------------------------------------------------
            // SOCKET TIMEOUT for keepalive to work.
            //
            // ws.read() blocks until a frame arrives. If no executionReport comes for
            // 60 minutes the listen key expires and the connection dies silently — the
            // keepalive check at the top of the loop never runs.
            //
            // Fix: set a read deadline on the underlying TCP socket. When the deadline
            // fires, ws.read() returns with beast::error::timeout. We loop back,
            // check keepalive, reset the deadline, and resume reading.
            //
            // Fast read timeout (1s) to check running flag frequently for responsive shutdown.
            // Keepalive interval = 30min. The timeout fires ~1800 times per keepalive window,
            // keeping the keepalive check alive even during quiet periods with no executionReports.
            // ---------------------------------------------------------------------------
            static constexpr auto READ_TIMEOUT = std::chrono::seconds(1);

            while (running.load()) {
                // --- Keepalive check ---
                int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();

                if ((now_ms - last_keepalive_ms) >= KEEPALIVE_INTERVAL_MS) {
                    try {
                        rest.keepalive_listen_key(listen_key);
                        std::cout << "[USER_WS] Keepalive sent\n";
                        last_keepalive_ms = now_ms;
                    } catch (const std::exception& e) {
                        std::cout << "[USER_WS] Keepalive failed: " << e.what() << "\n";
                        break;  // Listen key dead — reconnect + new key
                    }
                }

                // ---------------------------------------------------------------------------
                // Read frame with timeout.
                //
                // Beast sync ws.read() blocks forever — no timeout API on this Boost
                // version (basic_stream_socket has no expires_after). We arm a cancel
                // thread that cancels the underlying TCP socket after READ_TIMEOUT.
                // The cancel causes ws.read() to return with operation_cancelled,
                // which we treat as a timeout and loop back to the keepalive check.
                //
                // On a normal read (frame arrives before timeout), we set read_done
                // so the cancel thread exits cleanly without touching the socket.
                // ---------------------------------------------------------------------------
                beast::flat_buffer buffer;
                beast::error_code  ec;
                std::atomic<bool>  read_done{false};

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
                    if (ec == asio::error::operation_aborted) {
                        // Timeout — loop back to keepalive check
                        continue;
                    }
                    // Real error — reconnect
                    std::cout << "[USER_WS] Read error: " << ec.message() << "\n";
                    break;
                }

                std::string msg = beast::buffers_to_string(buffer.data());
                parse_message(msg);
            }

            // Inner read loop exited — signal WS dead.
            ctx_.ws_user_alive.store(false);

        } catch (const std::exception& e) {
            ctx_.ws_user_alive.store(false);
            if (running.load()) {
                // ---------------------------------------------------------------------------
                // In shadow mode (DISARMED), user stream is not required — fills are
                // simulated from the book. Log once, then back off to 60s to avoid spam.
                // In live mode (ARMED), keep the 2s retry — user stream is critical.
                // ---------------------------------------------------------------------------
                static bool logged_shadow_warning = false;
                if (!ctx_.arm.live_enabled() && !logged_shadow_warning) {
                    std::cout << "[USER_WS] Shadow mode — user stream unavailable (" << e.what()
                              << "). Shadow fills proceed from market book. Backing off to 60s.\n";
                    logged_shadow_warning = true;
                } else if (ctx_.arm.live_enabled()) {
                    std::cout << "[USER_WS] Reconnect (" << e.what() << ")\n";
                }

                auto backoff = ctx_.arm.live_enabled()
                    ? std::chrono::seconds(2)
                    : std::chrono::seconds(60);
                std::this_thread::sleep_for(backoff);
            }
        }
    }
}

void BinanceWSUser::parse_message(const std::string& msg) {
    // ---------------------------------------------------------------------------
    // executionReport event structure (USDT-M futures):
    //   e — event type ("executionReport")
    //   s — symbol (e.g. "BTCUSDT")
    //   c — client order ID (our client_id)
    //   i — order ID (exchange-assigned ID)
    //   x — execution type: NEW, TRADE, CANCELED, REJECTED, EXPIRED, ...
    //   X — order status
    //   l — last executed quantity (filled this tick)
    //   L — last executed price
    // ---------------------------------------------------------------------------
    try {
        json j = json::parse(msg);

        if (!j.contains("e") || j["e"].get<std::string>() != "executionReport")
            return;

        if (!j.contains("x")) return;
        std::string exec_type = j["x"].get<std::string>();

        std::string client_id = j.contains("c") ? j["c"].get<std::string>() : "";
        std::string exch_id   = j.contains("i") ? std::to_string(j["i"].get<int64_t>()) : "";
        std::string symbol    = j.contains("s") ? j["s"].get<std::string>() : "";

        // --- NEW: order acknowledged by exchange ---
        if (exec_type == "NEW") {
            if (client_id.empty()) return;
            ctx_.osm.on_ack(client_id, exch_id);
            ctx_.recorder.write_ack(client_id, exch_id);
            ctx_.latency.on_ack(client_id);

            std::cout << "[USER_WS] ACK: " << symbol << " client=" << client_id
                      << " exch=" << exch_id << "\n";
            return;
        }

        // --- TRADE: fill occurred ---
        if (exec_type == "TRADE") {
            if (exch_id.empty()) return;

            double last_qty   = std::stod(j["l"].get<std::string>());
            double last_price = std::stod(j["L"].get<std::string>());
            if (last_qty <= 0.0) return;

            // ---------------------------------------------------------------------------
            // OSM state transition — must happen first so status reflects fill.
            // ---------------------------------------------------------------------------
            ctx_.osm.on_fill(exch_id, last_qty);

            // Forensic recorder
            ctx_.recorder.write_fill(client_id, last_qty, last_price);

            // ---------------------------------------------------------------------------
            // LIVE FILL PIPELINE — all safety systems must see this fill.
            //
            // In shadow mode, fills are generated by ExecutionRouter::poll() which
            // runs the full pipeline inline. In live mode, fills arrive HERE
            // asynchronously from the user stream. Without this block, the live
            // path was invisible to risk, PnL, edge, desk, and telemetry.
            //
            // We need the original signed qty to update position correctly.
            // OSM tracks the order but by this point qty may have been decremented
            // by on_fill(). We look up the order record to get the symbol and
            // determine side from client_id prefix convention. However, the most
            // reliable source is the original order record — but on_fill already
            // modified qty. We use the sign convention: look up via client_id
            // to get engine_id, and reconstruct signed qty from the execution report.
            //
            // Binance "S" field = side of the order: "BUY" or "SELL".
            // ---------------------------------------------------------------------------
            std::string side = j.contains("S") ? j["S"].get<std::string>() : "";
            double signed_qty = (side == "BUY") ? last_qty : -last_qty;

            // Risk: update position tracking
            ctx_.risk.on_execution_ack(symbol, signed_qty);

            // ---------------------------------------------------------------------------
            // PnL + Edge + Desk: need engine_id and fill quality vs mid.
            // engine_id is embedded in client_id format: "<engine_id>_<seq>".
            // Extract it by finding the last underscore.
            // ---------------------------------------------------------------------------
            std::string engine_id;
            {
                size_t last_underscore = client_id.rfind('_');
                if (last_underscore != std::string::npos && last_underscore > 0) {
                    engine_id = client_id.substr(0, last_underscore);
                }
            }

            if (!engine_id.empty()) {
                // PnL: entry quality vs current mid
                TopOfBook tb = ctx_.queue.top(symbol);
                if (tb.valid) {
                    double mid = (tb.bid + tb.ask) * 0.5;
                    // pnl_delta = (mid - fill_price) * signed_qty
                    // Positive = bought below mid or sold above mid (good fill)
                    double pnl_delta = (mid - last_price) * signed_qty;
                    ctx_.pnl.update_fill(engine_id, pnl_delta);

                    double notional = last_price * last_qty;

                    // Edge Attribution: realized PnL at fill
                    if (ctx_.edge && notional > 0.0) {
                        double realized_bps = (pnl_delta / notional) * 10000.0;
                        // Use actual ACK latency for this fill
                        double lat_us = static_cast<double>(ctx_.latency.last_latency_us());
                        ctx_.edge->on_fill(client_id, realized_bps, lat_us);
                    }

                    // Desk Arbiter: feed fill PnL
                    if (ctx_.desk && notional > 0.0) {
                        double pnl_bps = (pnl_delta / notional) * 10000.0;
                        ctx_.desk->on_fill(engine_id, pnl_bps);
                    }
                }

                // Telemetry
                {
                    auto positions = ctx_.risk.dump_positions();
                    auto it = positions.find(symbol);
                    double pos_qty  = (it != positions.end()) ? it->second : 0.0;
                    double notional = std::fabs(pos_qty * last_price);
                    ctx_.telemetry.update_symbol(symbol, pos_qty, notional);
                }
            }

            std::cout << "[USER_WS] FILL: " << symbol
                      << " qty=" << last_qty << " px=" << last_price << "\n";
            return;
        }

        // --- CANCELED: order canceled (by us or by exchange) ---
        if (exec_type == "CANCELED") {
            if (exch_id.empty()) return;
            ctx_.osm.on_cancel(exch_id);
            ctx_.recorder.write_cancel(client_id);

            // Edge Attribution: clean up pending_ entry for this order.
            // Without this, canceled orders leak memory in pending_ forever.
            if (ctx_.edge) {
                ctx_.edge->on_cancel(client_id);
            }

            std::cout << "[USER_WS] CANCEL: " << symbol << " client=" << client_id << "\n";
            return;
        }

        // --- REJECTED / EXPIRED: order rejected or timed out ---
        if (exec_type == "REJECTED" || exec_type == "EXPIRED") {
            if (client_id.empty()) return;
            ctx_.osm.on_reject(client_id);
            ctx_.recorder.write_reject(client_id);

            // Edge Attribution: clean up pending_ entry
            if (ctx_.edge) {
                ctx_.edge->on_cancel(client_id);
            }

            std::cout << "[USER_WS] REJECT/EXPIRE: " << symbol
                      << " client=" << client_id << " type=" << exec_type << "\n";
            return;
        }

        // All other exec types (REPLACE, etc) — ignored for now

    } catch (const json::parse_error&) {
        // Subscription confirmations etc — skip
    } catch (const std::exception& e) {
        std::cout << "[USER_WS] Parse error: " << e.what() << "\n";
    }
}
