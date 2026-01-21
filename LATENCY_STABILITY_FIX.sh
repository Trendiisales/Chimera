#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -e

echo "[CHIMERA] Installing stable RTT tracking (no zero / stale detection)"

cat > exchange/BinanceWSClient.cpp << 'BWC'
#include "BinanceWSClient.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <chrono>
#include <cmath>
#include <atomic>

#include <boost/beast/ssl.hpp>
#include <boost/asio/ssl.hpp>

using tcp = boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;
namespace ssl = boost::asio::ssl;
using json = nlohmann::json;

static uint64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

BinanceWSClient::BinanceWSClient(const std::string& host,
                                 const std::string& port,
                                 const std::string& stream)
    : host_(host), port_(port), stream_(stream) {}

void BinanceWSClient::setCallback(TickCB cb) {
    cb_ = cb;
}

void BinanceWSClient::start() {
    running_ = true;
    worker_ = std::thread(&BinanceWSClient::run, this);
}

void BinanceWSClient::stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

void BinanceWSClient::run() {
    try {
        boost::asio::io_context ioc;

        ssl::context ctx{ssl::context::tlsv12_client};
        ctx.set_default_verify_paths();

        tcp::resolver resolver{ioc};

        websocket::stream<
            boost::beast::ssl_stream<tcp::socket>
        > ws{ioc, ctx};

        auto const results = resolver.resolve(host_, port_);
        boost::asio::connect(ws.next_layer().next_layer(),
                             results.begin(),
                             results.end());

        ws.next_layer().handshake(ssl::stream_base::client);

        std::string target = "/stream?streams=" + stream_;
        ws.handshake(host_, target);

        std::atomic<uint64_t> last_ping_us{0};
        std::atomic<uint64_t> last_rtt_us{0};
        std::atomic<uint64_t> last_pong_us{0};

        ws.control_callback(
            [&](websocket::frame_type kind,
                boost::beast::string_view) {
                if (kind == websocket::frame_type::pong) {
                    uint64_t now = now_us();
                    uint64_t sent = last_ping_us.load();
                    if (sent > 0 && now > sent) {
                        last_rtt_us.store(now - sent);
                        last_pong_us.store(now);
                    }
                }
            });

        auto last_ping_sent = std::chrono::steady_clock::now();

        float last_bid_sz = 0.0f;
        float last_ask_sz = 0.0f;
        float last_ofi = 0.0f;

        while (running_) {
            // Send ping every 1s
            auto now = std::chrono::steady_clock::now();
            if (now - last_ping_sent > std::chrono::seconds(1)) {
                last_ping_us.store(now_us());
                ws.ping({});
                last_ping_sent = now;
            }

            boost::beast::flat_buffer buffer;
            ws.read(buffer);

            std::string msg = boost::beast::buffers_to_string(buffer.data());
            auto j = json::parse(msg, nullptr, false);
            if (j.is_discarded()) continue;
            if (!j.contains("data")) continue;

            auto& d = j["data"];

            tier3::TickData t;

            // ============================
            // BOOK DATA
            // ============================
            if (d.contains("b") && d.contains("a")) {
                t.bid = std::stof(d["b"].get<std::string>());
                t.ask = std::stof(d["a"].get<std::string>());
            }
            if (d.contains("B")) {
                t.bid_sz = std::stof(d["B"].get<std::string>());
            }
            if (d.contains("A")) {
                t.ask_sz = std::stof(d["A"].get<std::string>());
            }

            // ============================
            // REAL OFI (DEPTH DELTA)
            // ============================
            float d_bid = t.bid_sz - last_bid_sz;
            float d_ask = t.ask_sz - last_ask_sz;
            float ofi = d_bid - d_ask;

            t.ofi_z = ofi;
            t.ofi_accel = ofi - last_ofi;

            last_bid_sz = t.bid_sz;
            last_ask_sz = t.ask_sz;
            last_ofi = ofi;

            // ============================
            // MICROSTRUCTURE FEATURES
            // ============================
            if (t.bid > 0 && t.ask > 0) {
                t.spread_bps =
                    ((t.ask - t.bid) / ((t.ask + t.bid) * 0.5f)) * 10000.0f;
            }

            t.impulse_bps = std::fabs(t.ofi_accel) * 10.0f;
            t.depth_ratio =
                (t.ask_sz > 0.0f)
                    ? std::min(1.0f, t.bid_sz / t.ask_sz)
                    : 1.0f;

            t.btc_impulse = (t.impulse_bps > 12.0f) ? 1 : 0;
            t.liquidation_long = (t.ofi_z < -5.0f) ? 1 : 0;
            t.liquidation_short = (t.ofi_z > 5.0f) ? 1 : 0;

            // ============================
            // STABLE RTT
            // ============================
            uint64_t last_rtt = last_rtt_us.load();
            uint64_t last_pong = last_pong_us.load();
            uint64_t nowu = now_us();

            // If no pong in 2 seconds → mark stale
            if (last_pong == 0 || (nowu - last_pong) > 2000000ULL) {
                t.exchange_time_us = 0; // 0 means STALE
            } else {
                t.exchange_time_us = last_rtt;
            }

            if (cb_) cb_(stream_, t);
        }

        ws.close(websocket::close_code::normal);
    } catch (const std::exception& e) {
        std::cerr << "[WS] Error: " << e.what() << std::endl;
    }
}
BWC

echo "[CHIMERA] Stable RTT installed"
