#!/usr/bin/env bash
set -e

ROOT="/home/trader/Chimera"
SRC="$ROOT/src"
GUI="$SRC/gui"

mkdir -p "$GUI/panels"

############################################
# 1. GUI STATE MODEL
############################################
cat << 'EOC' > "$GUI/GuiState.hpp"
#pragma once
#include <string>
#include <map>

namespace chimera {

struct GuiLatency {
    double rtt_ms = 0.0;
    double jitter_ms = 0.0;
    std::string mode;
};

struct GuiHeatmapCell {
    int trades = 0;
    double pnl = 0.0;
};

struct GuiState {
    GuiLatency latency;
    std::map<std::string,
        std::map<std::string, GuiHeatmapCell>> heatmap;

    std::string last_decision;
    std::string kill_reason;
};

}
EOC

############################################
# 2. GUI BROADCAST EXTENSION
############################################
cat << 'EOC' > "$GUI/GuiIntelligenceBroadcaster.hpp"
#pragma once
#include "GUIBroadcaster.hpp"
#include "GuiState.hpp"
#include "../core/heatmap/Heatmap.hpp"

namespace chimera {

class GuiIntelligenceBroadcaster {
public:
    static GuiIntelligenceBroadcaster& instance();

    void update_latency(double rtt, double jitter, const std::string& mode);
    void update_heatmap(const Heatmap& h);
    void set_decision(const std::string& why);
    void set_kill(const std::string& why);

private:
    GuiIntelligenceBroadcaster();
    GuiState m_state;
};

}
EOC

cat << 'EOC' > "$GUI/GuiIntelligenceBroadcaster.cpp"
#include "GuiIntelligenceBroadcaster.hpp"

namespace chimera {

GuiIntelligenceBroadcaster::GuiIntelligenceBroadcaster() {}

GuiIntelligenceBroadcaster&
GuiIntelligenceBroadcaster::instance() {
    static GuiIntelligenceBroadcaster inst;
    return inst;
}

void GuiIntelligenceBroadcaster::update_latency(double rtt,
                                                double jitter,
                                                const std::string& mode) {
    m_state.latency.rtt_ms = rtt;
    m_state.latency.jitter_ms = jitter;
    m_state.latency.mode = mode;
    GUIBroadcaster::instance().broadcast("latency", rtt);
}

void GuiIntelligenceBroadcaster::update_heatmap(const Heatmap& h) {
    m_state.heatmap.clear();
    for (auto& hr : h.grid()) {
        for (auto& md : hr.second) {
            m_state.heatmap[hr.first][md.first] = {
                md.second.trades,
                md.second.pnl
            };
        }
    }
    GUIBroadcaster::instance().broadcast("heatmap", m_state.heatmap);
}

void GuiIntelligenceBroadcaster::set_decision(const std::string& why) {
    m_state.last_decision = why;
    GUIBroadcaster::instance().broadcast("decision", why);
}

void GuiIntelligenceBroadcaster::set_kill(const std::string& why) {
    m_state.kill_reason = why;
    GUIBroadcaster::instance().broadcast("kill", why);
}

}
EOC

############################################
# 3. GUI PANELS (STATIC FRONTEND)
############################################
mkdir -p "$ROOT/gui"

cat << 'EOC' > "$ROOT/gui/index.html"
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Chimera Gold Desk</title>
<style>
body { font-family: monospace; background:#0b0e11; color:#d1d4dc; }
.panel { border:1px solid #333; padding:10px; margin:10px; }
.green { color:#00ff88; }
.yellow { color:#ffcc00; }
.red { color:#ff4444; }
table { border-collapse: collapse; }
td,th { border:1px solid #333; padding:4px; }
</style>
</head>
<body>

<h2>CHIMERA GOLD DESK</h2>

<div class="panel" id="latency">
<h3>Latency</h3>
<div id="mode"></div>
<div id="rtt"></div>
<div id="jitter"></div>
</div>

<div class="panel">
<h3>Last Decision</h3>
<pre id="decision"></pre>
</div>

<div class="panel">
<h3>Kill Switch</h3>
<pre id="kill"></pre>
</div>

<div class="panel">
<h3>PnL Heatmap</h3>
<table id="heatmap"></table>
</div>

<script>
const ws = new WebSocket("ws://" + location.hostname + ":7777");

ws.onmessage = ev => {
  const msg = JSON.parse(ev.data);

  if (msg.type === "latency") {
    document.getElementById("rtt").innerText = "RTT: " + msg.data.toFixed(2) + " ms";
  }
  if (msg.type === "decision") {
    document.getElementById("decision").innerText = msg.data;
  }
  if (msg.type === "kill") {
    document.getElementById("kill").innerText = msg.data;
  }
  if (msg.type === "heatmap") {
    const table = document.getElementById("heatmap");
    table.innerHTML = "";
    for (const hour in msg.data) {
      const row = document.createElement("tr");
      const th = document.createElement("th");
      th.innerText = hour;
      row.appendChild(th);
      for (const mode in msg.data[hour]) {
        const td = document.createElement("td");
        td.innerText = msg.data[hour][mode].pnl.toFixed(2);
        row.appendChild(td);
      }
      table.appendChild(row);
    }
  }
};
</script>

</body>
</html>
EOC

############################################
# 4. CMAKE WIRING
############################################
CMAKE="$ROOT/CMakeLists.txt"
if ! grep -q GuiIntelligenceBroadcaster "$CMAKE"; then
    sed -i '/target_sources(chimera PRIVATE/a \
    src/gui/GuiIntelligenceBroadcaster.cpp' "$CMAKE"
fi

############################################
# 5. BUILD
############################################
cd "$ROOT/build"
cmake ..
make -j$(nproc)

echo "[GOLD DESK GUI] Intelligence panels installed"
