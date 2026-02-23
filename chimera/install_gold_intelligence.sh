#!/usr/bin/env bash
set -e

ROOT="/home/trader/Chimera"
SRC="$ROOT/src"
BUILD="$ROOT/build"

mkdir -p "$SRC/core/attribution"
mkdir -p "$SRC/core/heatmap"
mkdir -p "$SRC/core/regime"

############################################
# 1. PnL ATTRIBUTION ENGINE
############################################
cat << 'EOC' > "$SRC/core/attribution/PnLAttribution.hpp"
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace chimera {

struct TradeAttribution {
    std::string symbol;
    std::string session;
    std::string mode;

    double panic_score;
    double safety_score;
    double latency_score;
    double confidence;

    std::string entry_style;
    std::string exit_style;

    double rtt_ms;
    double jitter_ms;

    double pnl_usd;
    double pnl_bps;
    uint64_t hold_time_ms;
};

class PnLAttributionEngine {
public:
    void record(const TradeAttribution& t);
    const std::vector<TradeAttribution>& all() const;

private:
    std::vector<TradeAttribution> m_trades;
};

}
EOC

cat << 'EOC' > "$SRC/core/attribution/PnLAttribution.cpp"
#include "PnLAttribution.hpp"

namespace chimera {

void PnLAttributionEngine::record(const TradeAttribution& t) {
    m_trades.push_back(t);
}

const std::vector<TradeAttribution>& PnLAttributionEngine::all() const {
    return m_trades;
}

}
EOC

############################################
# 2. HEATMAP AGGREGATOR
############################################
cat << 'EOC' > "$SRC/core/heatmap/Heatmap.hpp"
#pragma once
#include <map>
#include <string>

namespace chimera {

struct HeatmapCell {
    int trades = 0;
    double pnl = 0.0;
};

class Heatmap {
public:
    void add(const std::string& hour,
             const std::string& mode,
             double pnl);

    HeatmapCell get(const std::string& hour,
                    const std::string& mode) const;

    const std::map<std::string,
          std::map<std::string, HeatmapCell>>& grid() const;

private:
    std::map<std::string,
        std::map<std::string, HeatmapCell>> m_grid;
};

}
EOC

cat << 'EOC' > "$SRC/core/heatmap/Heatmap.cpp"
#include "Heatmap.hpp"

namespace chimera {

void Heatmap::add(const std::string& hour,
                  const std::string& mode,
                  double pnl) {
    auto& cell = m_grid[hour][mode];
    cell.trades += 1;
    cell.pnl += pnl;
}

HeatmapCell Heatmap::get(const std::string& hour,
                         const std::string& mode) const {
    return m_grid.at(hour).at(mode);
}

const std::map<std::string,
      std::map<std::string, HeatmapCell>>&
Heatmap::grid() const {
    return m_grid;
}

}
EOC

############################################
# 3. NEWS / CPI / NFP KILL MAP
############################################
cat << 'EOC' > "$SRC/core/regime/NewsKillMap.hpp"
#pragma once
#include <string>
#include <set>

namespace chimera {

class NewsKillMap {
public:
    NewsKillMap();

    bool is_blocked(const std::string& event) const;

private:
    std::set<std::string> m_blocked;
};

}
EOC

cat << 'EOC' > "$SRC/core/regime/NewsKillMap.cpp"
#include "NewsKillMap.hpp"

namespace chimera {

NewsKillMap::NewsKillMap() {
    m_blocked.insert("CPI");
    m_blocked.insert("NFP");
    m_blocked.insert("FOMC");
    m_blocked.insert("FED_SPEECH");
}

bool NewsKillMap::is_blocked(const std::string& event) const {
    return m_blocked.count(event) > 0;
}

}
EOC

############################################
# 4. AUTO-TUNER (READS HEATMAP)
############################################
cat << 'EOC' > "$SRC/core/regime/AutoTuner.hpp"
#pragma once
#include "../heatmap/Heatmap.hpp"

namespace chimera {

class AutoTuner {
public:
    AutoTuner(const Heatmap& h);

    bool allow_trade(const std::string& hour,
                     const std::string& mode) const;

private:
    const Heatmap& m_heatmap;
};

}
EOC

cat << 'EOC' > "$SRC/core/regime/AutoTuner.cpp"
#include "AutoTuner.hpp"

namespace chimera {

AutoTuner::AutoTuner(const Heatmap& h)
    : m_heatmap(h) {}

bool AutoTuner::allow_trade(const std::string& hour,
                            const std::string& mode) const {
    auto cell = m_heatmap.get(hour, mode);
    if (cell.trades < 5) return true;
    return cell.pnl > 0.0;
}

}
EOC

############################################
# 5. CMAKE WIRING
############################################
CMAKE="$ROOT/CMakeLists.txt"
if ! grep -q PnLAttribution "$CMAKE"; then
    sed -i '/target_sources(chimera PRIVATE/a \
    src/core/attribution/PnLAttribution.cpp \
    src/core/heatmap/Heatmap.cpp \
    src/core/regime/NewsKillMap.cpp \
    src/core/regime/AutoTuner.cpp' "$CMAKE"
fi

############################################
# 6. BUILD
############################################
cd "$BUILD"
cmake ..
make -j$(nproc)

echo "[GOLD DESK] Attribution + Heatmap + KillMap + AutoTuner installed"
