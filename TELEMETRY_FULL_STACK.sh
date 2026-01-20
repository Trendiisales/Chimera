#!/usr/bin/env bash
set -e

echo "[CHIMERA] INSTALLING FULL TELEMETRY BUS + FLIGHT DECK"

############################################
# STRUCTURE
############################################
mkdir -p telemetry/vendor
mkdir -p telemetry

############################################
# CPP-HTTPLIB
############################################
if [ ! -f telemetry/vendor/httplib.h ]; then
  echo "[FETCH] cpp-httplib"
  curl -L https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h \
    -o telemetry/vendor/httplib.h
fi

############################################
# TELEMETRY BUS
############################################
cat > telemetry/TelemetryBus.hpp << 'TEOF'
#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <map>
#include <chrono>

struct TelemetryEvent {
    std::string type;
    std::map<std::string, double> fields;
    uint64_t ts;
};

class TelemetryBus {
public:
    static TelemetryBus& instance() {
        static TelemetryBus bus;
        return bus;
    }

    void publish(const std::string& type,
                 const std::map<std::string, double>& fields) {
        std::lock_guard<std::mutex> lock(mtx_);
        TelemetryEvent e;
        e.type = type;
        e.fields = fields;
        e.ts = now();
        events_.push_back(e);
        if (events_.size() > max_events_) {
            events_.erase(events_.begin());
        }
    }

    std::vector<TelemetryEvent> snapshot() {
        std::lock_guard<std::mutex> lock(mtx_);
        return events_;
    }

private:
    TelemetryBus() = default;

    uint64_t now() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    std::mutex mtx_;
    std::vector<TelemetryEvent> events_;
    size_t max_events_ = 10000;
};
TEOF

############################################
# TELEMETRY BOOT
############################################
cat > telemetry/telemetry_boot.hpp << 'TEOF'
#pragma once
void startTelemetry();
TEOF

############################################
# FLIGHT DECK SERVER
############################################
cat > telemetry/FlightDeckServer.cpp << 'TEOF'
#include "TelemetryBus.hpp"
#include "telemetry_boot.hpp"
#include "vendor/httplib.h"

#include <sstream>
#include <thread>
#include <iostream>

static std::thread server_thread;

static std::string render_html() {
    auto events = TelemetryBus::instance().snapshot();
    std::ostringstream out;

    out << "<html><head>";
    out << "<meta http-equiv='refresh' content='1'>";
    out << "<style>";
    out << "body{background:#0b0f14;color:#ddd;font-family:monospace}";
    out << "h2{color:#6cf}";
    out << "table{border-collapse:collapse;width:100%}";
    out << "td,th{border:1px solid #333;padding:4px}";
    out << "</style>";
    out << "</head><body>";
    out << "<h2>CHIMERA FLIGHT DECK</h2>";

    out << "<table>";
    out << "<tr><th>TS</th><th>TYPE</th><th>FIELDS</th></tr>";

    for (auto it = events.rbegin();
         it != events.rend() && std::distance(events.rbegin(), it) < 200;
         ++it) {
        out << "<tr>";
        out << "<td>" << it->ts << "</td>";
        out << "<td>" << it->type << "</td>";
        out << "<td>";
        for (auto& kv : it->fields) {
            out << kv.first << "=" << kv.second << " ";
        }
        out << "</td></tr>";
    }

    out << "</table>";
    out << "</body></html>";
    return out.str();
}

void startTelemetry() {
    server_thread = std::thread([]() {
        httplib::Server svr;

        svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
            res.set_content(render_html(), "text/html");
        });

        std::cout << "[FLIGHTDECK] Listening on 0.0.0.0:8080\n";
        svr.listen("0.0.0.0", 8080);
    });

    server_thread.detach();
}
TEOF

############################################
# PATCH MAIN.CPP
############################################
echo "[PATCH] Wiring telemetry into main.cpp"

cp main.cpp main.cpp.bak.$(date +%s)

if ! grep -q "telemetry/telemetry_boot.hpp" main.cpp; then
  sed -i '1i #include "telemetry/telemetry_boot.hpp"\n#include "telemetry/TelemetryBus.hpp"' main.cpp
fi

if ! grep -q "startTelemetry();" main.cpp; then
  awk '
    /int main/ {
      print
      print "    startTelemetry();"
      next
    }
    { print }
  ' main.cpp > main.cpp.tmp
  mv main.cpp.tmp main.cpp
fi

############################################
# REBUILD
############################################
echo "[CHIMERA] REBUILDING"
cd build
make -j$(nproc)

echo
echo "[CHIMERA] TELEMETRY CORE ONLINE"
echo "OPEN IN CHROME:"
echo "http://15.168.16.103:8080"
echo
