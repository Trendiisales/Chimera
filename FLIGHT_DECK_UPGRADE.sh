#!/usr/bin/env bash
set -e

echo "[CHIMERA] DEPLOYING FLIGHT DECK TELEMETRY + OPERATOR CONSOLE"

############################################
# FLIGHT DECK SERVER (JSON + GUI)
############################################
mkdir -p telemetry

cat > telemetry/FlightDeckServer.cpp << 'TEOF'
#include "TelemetryBus.hpp"
#include <thread>
#include <sstream>
#include "external/httplib.h"

static std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

static std::string to_json() {
    auto events = TelemetryBus::instance().snapshot();
    std::ostringstream o;
    o << "{ \"events\": [";
    for (size_t i = 0; i < events.size(); ++i) {
        auto& e = events[i];
        o << "{";
        o << "\"type\":\"" << json_escape(e.type) << "\",";
        o << "\"ts\":" << e.ts << ",";
        o << "\"fields\":{";
        size_t c = 0;
        for (auto& f : e.fields) {
            o << "\"" << json_escape(f.first) << "\":"
              << "\"" << json_escape(f.second) << "\"";
            if (++c < e.fields.size()) o << ",";
        }
        o << "}}";
        if (i + 1 < events.size()) o << ",";
    }
    o << "]}";
    return o.str();
}

void startTelemetry() {
    std::thread([]() {
        httplib::Server svr;

        svr.Get("/api/events", [](const httplib::Request&, httplib::Response& res) {
            res.set_content(to_json(), "application/json");
        });

        svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
            res.set_content(R"HTML(
<!DOCTYPE html>
<html>
<head>
<title>CHIMERA FLIGHT DECK</title>
<style>
body {
    margin: 0;
    background: #0b0e11;
    color: #e0e0e0;
    font-family: monospace;
}
header {
    padding: 12px;
    background: #111;
    border-bottom: 1px solid #333;
}
h1 {
    margin: 0;
    font-size: 20px;
    letter-spacing: 1px;
}
.panel {
    padding: 10px;
    border-bottom: 1px solid #222;
}
table {
    width: 100%;
    border-collapse: collapse;
}
th, td {
    padding: 4px 8px;
    border-bottom: 1px solid #222;
}
th {
    color: #aaa;
    text-align: left;
}
.good { color: #00ff9c; }
.bad { color: #ff4c4c; }
.warn { color: #ffaa00; }
</style>
</head>
<body>
<header>
<h1>CHIMERA — FLIGHT DECK</h1>
</header>

<div class="panel">
<h3>Capital Flow</h3>
<table id="flow">
<tr><th>Engine</th><th>Net</th><th>Alloc %</th></tr>
</table>
</div>

<div class="panel">
<h3>Engine Health</h3>
<table id="health">
<tr><th>Engine</th><th>Metric</th><th>Value</th></tr>
</table>
</div>

<div class="panel">
<h3>Trades (Last 50)</h3>
<table id="trades">
<tr><th>Time</th><th>Symbol</th><th>Side</th><th>Qty</th><th>Price</th></tr>
</table>
</div>

<script>
async function update() {
    try {
        const res = await fetch("/api/events");
        const data = await res.json();

        const flow = document.getElementById("flow");
        const health = document.getElementById("health");
        const trades = document.getElementById("trades");

        flow.innerHTML = "<tr><th>Engine</th><th>Net</th><th>Alloc %</th></tr>";
        health.innerHTML = "<tr><th>Engine</th><th>Metric</th><th>Value</th></tr>";
        trades.innerHTML = "<tr><th>Time</th><th>Symbol</th><th>Side</th><th>Qty</th><th>Price</th></tr>";

        data.events.slice(-200).forEach(e => {
            if (e.type === "FLOW") {
                const r = flow.insertRow();
                r.insertCell().innerText = e.fields.engine || "";
                r.insertCell().innerText = e.fields.net || "";
                r.insertCell().innerText = e.fields.alloc || "";
            }

            if (e.type === "EDGE" || e.type === "MC" || e.type === "DIVERGE" || e.type === "RISK") {
                for (const k in e.fields) {
                    const r = health.insertRow();
                    r.insertCell().innerText = e.type;
                    r.insertCell().innerText = k;
                    r.insertCell().innerText = e.fields[k];
                }
            }

            if (e.type === "TRADE") {
                const r = trades.insertRow();
                r.insertCell().innerText = new Date(e.ts).toLocaleTimeString();
                r.insertCell().innerText = e.fields.symbol || "";
                r.insertCell().innerText = e.fields.side || "";
                r.insertCell().innerText = e.fields.qty || "";
                r.insertCell().innerText = e.fields.price || "";
            }
        });
    } catch {}
}

setInterval(update, 1000);
update();
</script>
</body>
</html>
)HTML", "text/html");
        });

        svr.listen("0.0.0.0", 8080);
    }).detach();
}
TEOF

############################################
# REBUILD
############################################
echo "[CHIMERA] REBUILDING"
cd build
make -j$(nproc)

echo
echo "[CHIMERA] FLIGHT DECK ONLINE"
echo "OPEN:"
echo "http://15.168.16.103:8080"
echo
