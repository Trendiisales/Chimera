#!/usr/bin/env bash
set -e

echo "[CHIMERA] HARD PATCHING TELEMETRY + OPERATOR STACK"

########################################
# TELEMETRY BUS — AUTHORITATIVE
########################################
mkdir -p telemetry

cat > telemetry/TelemetryBus.hpp << 'TELEOF'
#pragma once
#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <chrono>

struct TelemetryEvent {
    std::string type;
    uint64_t ts;
    std::map<std::string, std::string> fields;
};

class TelemetryBus {
    std::mutex mtx;
    std::vector<TelemetryEvent> ring;
    size_t max_events = 256;

    TelemetryBus() {}

public:
    static TelemetryBus& instance() {
        static TelemetryBus bus;
        return bus;
    }

    void push(const std::string& type,
              const std::map<std::string, std::string>& fields) {
        std::lock_guard<std::mutex> lock(mtx);

        TelemetryEvent e;
        e.type = type;
        e.ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
        e.fields = fields;

        ring.push_back(e);
        if (ring.size() > max_events)
            ring.erase(ring.begin());
    }

    // COMPAT LAYER
    void publish(const std::string& type,
                 const std::map<std::string, std::string>& fields) {
        push(type, fields);
    }

    std::vector<TelemetryEvent> snapshot() {
        std::lock_guard<std::mutex> lock(mtx);
        return ring;
    }
};
TELEOF

########################################
# FLIGHT DECK SERVER — HTTP SNAPSHOT
########################################
mkdir -p telemetry

cat > telemetry/FlightDeckServer.cpp << 'FLIGHTEOF'
#include "external/httplib.h"
#include "TelemetryBus.hpp"
#include <sstream>

static std::string json_escape(const std::string& s) {
    std::ostringstream o;
    for (auto c : s) {
        switch (c) {
        case '"': o << "\\\""; break;
        case '\\': o << "\\\\"; break;
        case '\n': o << "\\n"; break;
        case '\r': o << "\\r"; break;
        case '\t': o << "\\t"; break;
        default: o << c; break;
        }
    }
    return o.str();
}

static std::string to_json() {
    auto events = TelemetryBus::instance().snapshot();
    std::ostringstream o;
    o << "[";

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

    o << "]";
    return o.str();
}

void startTelemetry(int port = 9090) {
    static httplib::Server svr;

    svr.Get("/snapshot", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(to_json(), "application/json");
    });

    std::thread([port]() {
        svr.listen("0.0.0.0", port);
    }).detach();
}
FLIGHTEOF

########################################
# OPERATOR SERVER — POLL MODE GUI
########################################
mkdir -p gui

cat > gui/live_operator_server.cpp << 'GUIEOF'
#include "external/httplib.h"
#include <thread>
#include <string>

static const char* PAGE = R"HTML(
<!DOCTYPE html>
<html>
<head>
<title>CHIMERA OPERATOR</title>
<style>
body {
  background: #0e1117;
  color: #d1d5db;
  font-family: monospace;
}
h1 { color: #e5e7eb; }
table {
  width: 100%;
  border-collapse: collapse;
}
td, th {
  border-bottom: 1px solid #333;
  padding: 4px;
}
</style>
</head>
<body>
<h1>CHIMERA LIVE</h1>
<div id="events"></div>

<script>
async function poll() {
  try {
    let r = await fetch("http://" + location.hostname + ":9090/snapshot");
    let j = await r.json();
    let html = "<table><tr><th>Time</th><th>Type</th><th>Fields</th></tr>";
    for (let e of j.reverse()) {
      let f = "";
      for (let k in e.fields) {
        f += k + "=" + e.fields[k] + " ";
      }
      html += "<tr><td>" + new Date(e.ts).toLocaleTimeString() +
              "</td><td>" + e.type +
              "</td><td>" + f + "</td></tr>";
    }
    html += "</table>";
    document.getElementById("events").innerHTML = html;
  } catch (e) {}
}

setInterval(poll, 1000);
poll();
</script>
</body>
</html>
)HTML";

void start_operator_console(int port) {
    static httplib::Server svr;

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(PAGE, "text/html");
    });

    std::thread([port]() {
        svr.listen("0.0.0.0", port);
    }).detach();
}
GUIEOF

########################################
# REBUILD
########################################
echo "[CHIMERA] REBUILDING"
cd build
make -j

echo
echo "[CHIMERA] DONE"
echo "Telemetry: http://15.168.16.103:9090/snapshot"
echo "Console:   http://15.168.16.103:8080"
