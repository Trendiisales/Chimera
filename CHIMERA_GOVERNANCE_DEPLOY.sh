#!/usr/bin/env bash
set -e

[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }

echo "[CHIMERA] Deploying Governance Stack (GLOBAL KILL, FADE LIVE, SIM CAPITAL)"

############################################
# DIRECTORIES
############################################
mkdir -p core/include/governance
mkdir -p core/src/governance
mkdir -p telemetry
mkdir -p drawdown

############################################
# TELEMETRY GOVERNANCE TYPES
############################################
cat > telemetry/TelemetryGovernance.hpp << 'TEOF'
#pragma once
#include <string>
#include <cstdint>

struct TelemetryGovernanceRow {
    std::string session;
    std::string regime;
    std::string kill_mode;
    std::string trigger_engine;
    double      drawdown_bps;
    uint64_t    ts_us;
};
TEOF

############################################
# SESSION GOVERNOR
############################################
cat > core/include/governance/SessionGovernor.hpp << 'TEOF'
#pragma once
#include <string>
#include <cstdint>

class SessionGovernor {
public:
    enum class Session {
        ASIA,
        LONDON,
        NEWYORK,
        UNKNOWN
    };

    Session classify(uint64_t ts_us) const;
    std::string toString(Session s) const;
};
TEOF

cat > core/src/governance/SessionGovernor.cpp << 'TEOF'
#include "governance/SessionGovernor.hpp"

SessionGovernor::Session SessionGovernor::classify(uint64_t ts_us) const {
    uint64_t sec = ts_us / 1000000ULL;
    uint64_t h = (sec / 3600ULL) % 24ULL;

    if (h >= 0 && h < 7) return Session::ASIA;
    if (h >= 7 && h < 13) return Session::LONDON;
    if (h >= 13 && h < 20) return Session::NEWYORK;
    return Session::UNKNOWN;
}

std::string SessionGovernor::toString(Session s) const {
    switch (s) {
        case Session::ASIA: return "ASIA";
        case Session::LONDON: return "LONDON";
        case Session::NEWYORK: return "NEWYORK";
        default: return "UNKNOWN";
    }
}
TEOF

############################################
# REGIME CLASSIFIER
############################################
cat > core/include/governance/RegimeClassifier.hpp << 'TEOF'
#pragma once
#include <string>

class RegimeClassifier {
public:
    enum class Regime {
        RANGE,
        MEAN_REVERT,
        TREND,
        BREAKOUT
    };

    Regime classify(double volatility_bps, double impulse_bps) const;
    std::string toString(Regime r) const;
};
TEOF

cat > core/src/governance/RegimeClassifier.cpp << 'TEOF'
#include "governance/RegimeClassifier.hpp"

RegimeClassifier::Regime
RegimeClassifier::classify(double vol, double impulse) const {
    if (impulse > 10.0) return Regime::BREAKOUT;
    if (vol > 8.0) return Regime::TREND;
    if (vol < 3.0) return Regime::RANGE;
    return Regime::MEAN_REVERT;
}

std::string RegimeClassifier::toString(Regime r) const {
    switch (r) {
        case Regime::RANGE: return "RANGE";
        case Regime::MEAN_REVERT: return "MEAN_REVERT";
        case Regime::TREND: return "TREND";
        case Regime::BREAKOUT: return "BREAKOUT";
        default: return "UNKNOWN";
    }
}
TEOF

############################################
# CAPITAL LADDER
############################################
cat > core/include/governance/CapitalLadder.hpp << 'TEOF'
#pragma once
#include "governance/RegimeClassifier.hpp"

class CapitalLadder {
public:
    double scale(double base_qty,
                 RegimeClassifier::Regime regime,
                 double drawdown_bps) const;
};
TEOF

cat > core/src/governance/CapitalLadder.cpp << 'TEOF'
#include "governance/CapitalLadder.hpp"

double CapitalLadder::scale(double base,
                            RegimeClassifier::Regime r,
                            double dd) const {
    double regime_mult = 0.0;

    switch (r) {
        case RegimeClassifier::Regime::RANGE: regime_mult = 1.0; break;
        case RegimeClassifier::Regime::MEAN_REVERT: regime_mult = 1.2; break;
        case RegimeClassifier::Regime::TREND: regime_mult = 0.0; break;
        case RegimeClassifier::Regime::BREAKOUT: regime_mult = 0.0; break;
    }

    double dd_mult = 1.0;
    if (dd < -10.0) dd_mult = 0.5;
    if (dd < -20.0) dd_mult = 0.25;

    return base * regime_mult * dd_mult;
}
TEOF

############################################
# DRAWDOWN SENTINEL (GLOBAL KILL)
############################################
cat > drawdown/DrawdownSentinel.hpp << 'TEOF'
#pragma once
#include <unordered_map>
#include <string>

struct DrawdownStats {
    double peak_bps = 0.0;
    double trough_bps = 0.0;
};

class DrawdownSentinel {
public:
    bool update(const std::string& engine, double net_bps);
    double drawdown(const std::string& engine) const;
private:
    std::unordered_map<std::string, DrawdownStats> stats_;
};
TEOF

cat > drawdown/DrawdownSentinel.cpp << 'TEOF'
#include "DrawdownSentinel.hpp"

bool DrawdownSentinel::update(const std::string& e, double net) {
    auto& s = stats_[e];
    if (net > s.peak_bps) s.peak_bps = net;
    if (net < s.trough_bps) s.trough_bps = net;
    double dd = s.trough_bps - s.peak_bps;
    return dd <= -25.0;
}

double DrawdownSentinel::drawdown(const std::string& e) const {
    auto it = stats_.find(e);
    if (it == stats_.end()) return 0.0;
    return it->second.trough_bps - it->second.peak_bps;
}
TEOF

############################################
# TELEMETRY SERVER PATCH (GOVERNANCE JSON)
############################################
cat > telemetry/TelemetryServer.hpp << 'TEOF'
#pragma once
void runTelemetryServer(int port);
void setGovernanceJSON(const std::string& j);
TEOF

cat > telemetry/TelemetryServer.cpp << 'TEOF'
#include "TelemetryServer.hpp"
#include "TelemetryBus.hpp"
#include <httplib.h>
#include <sstream>
#include <atomic>

static std::atomic<const char*> GOV_JSON{nullptr};

void setGovernanceJSON(const std::string& j) {
    char* mem = new char[j.size()+1];
    std::copy(j.begin(), j.end(), mem);
    mem[j.size()] = 0;
    GOV_JSON.store(mem);
}

void runTelemetryServer(int port) {
    httplib::Server svr;

    svr.Get("/json", [](const httplib::Request&, httplib::Response& res) {
        std::ostringstream body;
        auto engines = TelemetryBus::instance().snapshotEngines();
        auto trades  = TelemetryBus::instance().snapshotTrades();

        body << "{ \"engines\": [";
        for (size_t i = 0; i < engines.size(); ++i) {
            const auto& e = engines[i];
            body << "{"
                 << "\"symbol\":\"" << e.symbol << "\","
                 << "\"net_bps\":" << e.net_bps << ","
                 << "\"dd_bps\":" << e.dd_bps << ","
                 << "\"trades\":" << e.trades << ","
                 << "\"fees\":" << e.fees << ","
                 << "\"alloc\":" << e.alloc << ","
                 << "\"leverage\":" << e.leverage << ","
                 << "\"state\":\"" << e.state << "\"}";
            if (i+1 < engines.size()) body << ",";
        }
        body << "], \"trades\": [";
        for (size_t i = 0; i < trades.size(); ++i) {
            const auto& t = trades[i];
            body << "{"
                 << "\"engine\":\"" << t.engine << "\","
                 << "\"symbol\":\"" << t.symbol << "\","
                 << "\"side\":\"" << t.side << "\","
                 << "\"bps\":" << t.bps << ","
                 << "\"latency_ms\":" << t.latency_ms << ","
                 << "\"leverage\":" << t.leverage << "}";
            if (i+1 < trades.size()) body << ",";
        }
        body << "]";

        const char* g = GOV_JSON.load();
        if (g) {
            body << ", \"governance\": " << g;
        }

        body << "}";

        res.set_content(body.str(), "application/json");
    });

    svr.listen("0.0.0.0", port);
}
TEOF

############################################
# CMAKE INJECT
############################################
echo "[CHIMERA] Injecting sources into CMakeLists.txt"

python3 - << 'PYEOF'
from pathlib import Path

cm = Path("CMakeLists.txt")
text = cm.read_text()

need = [
    "core/src/governance/SessionGovernor.cpp",
    "core/src/governance/RegimeClassifier.cpp",
    "core/src/governance/CapitalLadder.cpp",
    "drawdown/DrawdownSentinel.cpp"
]

for n in need:
    if n not in text:
        text = text.replace("add_executable(chimera",
                             "add_executable(chimera\n    " + "\n    ".join(need))

cm.write_text(text)
PYEOF

############################################
# BUILD
############################################
echo "[CHIMERA] Building"
rm -rf build
cmake -B build
cmake --build build -j

############################################
# RUN
############################################
echo "[CHIMERA] Launching"
./build/chimera
