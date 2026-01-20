#!/usr/bin/env bash
set -e

echo "[CHIMERA] Installing Full Telemetry + Operator Console"

#######################################
# DIRS
#######################################
mkdir -p telemetry
mkdir -p gui

#######################################
# TELEMETRY BUS
#######################################
cat > telemetry/TelemetryBus.hpp << 'EOT'
#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <chrono>

struct TradeEvent {
    std::string engine;
    std::string symbol;
    std::string side;
    double qty;
    double entry;
    double exit;
    double bps;
    double fees;
    double latency_ms;
    double leverage;
    uint64_t ts;
};

struct EngineStats {
    std::string name;
    double net_bps = 0;
    double dd_bps = 0;
    int trades = 0;
    double fees = 0;
    double alloc = 0;
    double leverage = 1.0;
    std::string status = "LIVE";
};

class TelemetryBus {
public:
    static TelemetryBus& instance() {
        static TelemetryBus bus;
        return bus;
    }

    void recordTrade(const TradeEvent& ev) {
        std::lock_guard<std::mutex> lock(mtx_);
        trades_.push_back(ev);
        if (trades_.size() > 500) {
            trades_.erase(trades_.begin());
        }
    }

    void updateEngine(const EngineStats& st) {
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto& e : engines_) {
            if (e.name == st.name) {
                e = st;
                return;
            }
        }
        engines_.push_back(st);
    }

    std::vector<TradeEvent> trades() {
        std::lock_guard<std::mutex> lock(mtx_);
        return trades_;
    }

    std::vector<EngineStats> engines() {
        std::lock_guard<std::mutex> lock(mtx_);
        return engines_;
    }

private:
    std::mutex mtx_;
    std::vector<TradeEvent> trades_;
    std::vector<EngineStats> engines_;
};
EOT

#######################################
# HTTP SERVER
#######################################
cat > telemetry/TelemetryServer.hpp << 'EOT'
#pragma once
#include <thread>
#include <atomic>

class TelemetryServer {
public:
    TelemetryServer(int port);
    void start();
    void stop();
private:
    void run();
    int port_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};
EOT

cat > telemetry/TelemetryServer.cpp << 'EOT'
#include "TelemetryServer.hpp"
#include "TelemetryBus.hpp"

#include <boost/asio.hpp>
#include <sstream>

using boost::asio::ip::tcp;

TelemetryServer::TelemetryServer(int port)
    : port_(port) {}

void TelemetryServer::start() {
    running_ = true;
    worker_ = std::thread(&TelemetryServer::run, this);
}

void TelemetryServer::stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

static std::string jsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"') o += "\\\"";
        else o += c;
    }
    return o;
}

void TelemetryServer::run() {
    boost::asio::io_context io;
    tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), port_));

    while (running_) {
        tcp::socket socket(io);
        acceptor.accept(socket);

        auto engines = TelemetryBus::instance().engines();
        auto trades = TelemetryBus::instance().trades();

        std::ostringstream body;
        body << "{ \"engines\": [";
        for (size_t i = 0; i < engines.size(); i++) {
            const auto& e = engines[i];
            body << "{";
            body << "\"name\":\"" << jsonEscape(e.name) << "\",";
            body << "\"net_bps\":" << e.net_bps << ",";
            body << "\"dd_bps\":" << e.dd_bps << ",";
            body << "\"trades\":" << e.trades << ",";
            body << "\"fees\":" << e.fees << ",";
            body << "\"alloc\":" << e.alloc << ",";
            body << "\"leverage\":" << e.leverage << ",";
            body << "\"status\":\"" << e.status << "\"";
            body << "}";
            if (i + 1 < engines.size()) body << ",";
        }
        body << "], \"trades\": [";

        for (size_t i = 0; i < trades.size(); i++) {
            const auto& t = trades[i];
            body << "{";
            body << "\"engine\":\"" << jsonEscape(t.engine) << "\",";
            body << "\"symbol\":\"" << jsonEscape(t.symbol) << "\",";
            body << "\"side\":\"" << jsonEscape(t.side) << "\",";
            body << "\"bps\":" << t.bps << ",";
            body << "\"latency_ms\":" << t.latency_ms << ",";
            body << "\"lev\":" << t.leverage;
            body << "}";
            if (i + 1 < trades.size()) body << ",";
        }
        body << "] }";

        std::ostringstream resp;
        resp << "HTTP/1.1 200 OK\r\n";
        resp << "Content-Type: application/json\r\n";
        resp << "Access-Control-Allow-Origin: *\r\n";
        resp << "Content-Length: " << body.str().size() << "\r\n\r\n";
        resp << body.str();

        boost::asio::write(socket, boost::asio::buffer(resp.str()));
    }
}
EOT

#######################################
# GUI
#######################################
cat > gui/operator_console.html << 'EOT'
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8"/>
<title>CHIMERA OPERATOR</title>
<style>
body { background:#0b0e14; color:#d0d0d0; font-family:monospace; }
table { border-collapse:collapse; width:100%; margin-bottom:20px; }
th, td { border:1px solid #333; padding:6px; text-align:center; }
th { background:#111; }
.dead { color:#aa3333; }
.live { color:#33aa33; }
</style>
</head>
<body>
<h2>CHIMERA — LIVE TELEMETRY</h2>

<h3>Engines</h3>
<table id="engines">
<tr>
<th>ENGINE</th><th>5M bps</th><th>DD</th><th>TRADES</th>
<th>FEES</th><th>ALLOC</th><th>LEV</th><th>STATUS</th>
</tr>
</table>

<h3>Recent Trades</h3>
<table id="trades">
<tr>
<th>ENGINE</th><th>SYMBOL</th><th>SIDE</th><th>BPS</th><th>LAT(ms)</th><th>LEV</th>
</tr>
</table>

<script>
const API = location.origin;

function refresh() {
    fetch(API)
    .then(r => r.json())
    .then(data => {
        const eTable = document.getElementById("engines");
        eTable.innerHTML = "<tr><th>ENGINE</th><th>5M bps</th><th>DD</th><th>TRADES</th><th>FEES</th><th>ALLOC</th><th>LEV</th><th>STATUS</th></tr>";
        data.engines.forEach(e => {
            const row = document.createElement("tr");
            row.className = e.status === "DEAD" ? "dead" : "live";
            row.innerHTML =
                "<td>"+e.name+"</td>" +
                "<td>"+e.net_bps.toFixed(2)+"</td>" +
                "<td>"+e.dd_bps.toFixed(2)+"</td>" +
                "<td>"+e.trades+"</td>" +
                "<td>"+e.fees.toFixed(2)+"</td>" +
                "<td>"+(e.alloc*100).toFixed(1)+"%</td>" +
                "<td>"+e.leverage.toFixed(1)+"x</td>" +
                "<td>"+e.status+"</td>";
            eTable.appendChild(row);
        });

        const tTable = document.getElementById("trades");
        tTable.innerHTML = "<tr><th>ENGINE</th><th>SYMBOL</th><th>SIDE</th><th>BPS</th><th>LAT(ms)</th><th>LEV</th></tr>";
        data.trades.slice(-20).reverse().forEach(t => {
            const row = document.createElement("tr");
            row.innerHTML =
                "<td>"+t.engine+"</td>" +
                "<td>"+t.symbol+"</td>" +
                "<td>"+t.side+"</td>" +
                "<td>"+t.bps.toFixed(2)+"</td>" +
                "<td>"+t.latency_ms.toFixed(1)+"</td>" +
                "<td>"+t.lev.toFixed(1)+"x</td>";
            tTable.appendChild(row);
        });
    });
}

setInterval(refresh, 1000);
</script>
</body>
</html>
EOT

#######################################
# BUILD HOOK
#######################################
cat > telemetry/telemetry_boot.hpp << 'EOT'
#pragma once
#include "TelemetryServer.hpp"

static TelemetryServer* g_server = nullptr;

inline void startTelemetry() {
    g_server = new TelemetryServer(8080);
    g_server->start();
}
EOT

echo "[CHIMERA] Telemetry + GUI installed"
echo "Serve GUI: http://<VPS-IP>:8080"
echo "File: gui/operator_console.html"
