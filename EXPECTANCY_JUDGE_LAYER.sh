#!/usr/bin/env bash
set -e

[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }

echo "[EXPECTANCY] Installing Expectancy Judge Layer"

mkdir -p expectancy

############################################
# EXPECTANCY ENGINE
############################################
cat > expectancy/ExpectancyJudge.hpp << 'HPP'
#pragma once
#include <unordered_map>
#include <deque>
#include <string>
#include <cmath>
#include <stdexcept>

struct ExpectancyStats {
    double win_rate = 0.0;
    double avg_win = 0.0;
    double avg_loss = 0.0;
    double expectancy = 0.0;
};

struct TradeSample {
    double pnl_bps;
};

class ExpectancyJudge {
public:
    explicit ExpectancyJudge(size_t window = 100)
        : window_(window) {}

    void record(const std::string& engine, double pnl_bps) {
        auto& q = history_[engine];
        q.push_back({pnl_bps});
        if (q.size() > window_)
            q.pop_front();
    }

    ExpectancyStats stats(const std::string& engine) const {
        auto it = history_.find(engine);
        if (it == history_.end() || it->second.empty())
            return {};

        const auto& q = it->second;

        double wins = 0;
        double losses = 0;
        double sum_win = 0;
        double sum_loss = 0;

        for (const auto& t : q) {
            if (t.pnl_bps > 0) {
                wins++;
                sum_win += t.pnl_bps;
            } else {
                losses++;
                sum_loss += std::abs(t.pnl_bps);
            }
        }

        ExpectancyStats s;
        double total = wins + losses;
        if (total == 0)
            return s;

        s.win_rate = wins / total;
        s.avg_win = wins > 0 ? sum_win / wins : 0.0;
        s.avg_loss = losses > 0 ? sum_loss / losses : 0.0;
        s.expectancy = (s.win_rate * s.avg_win) - ((1.0 - s.win_rate) * s.avg_loss);

        return s;
    }

    bool allowed(const std::string& engine) const {
        auto s = stats(engine);
        return s.expectancy >= 0.0;
    }

private:
    size_t window_;
    std::unordered_map<std::string, std::deque<TradeSample>> history_;
};
HPP

############################################
# INJECT INTO SHADOW EXECUTOR
############################################
cat > expectancy/INJECT_EXPECTANCY_JUDGE.sh << 'PATCH'
#!/usr/bin/env bash
set -e
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }

TARGET="execution/ShadowExecutor.cpp"
TMP=$(mktemp)

echo "[EXPECTANCY] Wiring Expectancy Judge into ShadowExecutor"

while IFS= read -r line; do
    echo "$line" >> "$TMP"

    if [[ "$line" == *"#include"* && "$line" == *"CapitalGovernor.hpp"* ]]; then
        echo '#include "expectancy/ExpectancyJudge.hpp"' >> "$TMP"
        echo 'static ExpectancyJudge __expectancy_judge(100);' >> "$TMP"
    fi

    if [[ "$line" == *"TelemetryBus::instance().recordTrade"* ]]; then
        echo '    __expectancy_judge.record(row.engine, row.bps);' >> "$TMP"
        echo '    if (!__expectancy_judge.allowed(row.engine)) {' >> "$TMP"
        echo '        throw std::runtime_error("EXPECTANCY KILL: Engine " + row.engine + " went negative");' >> "$TMP"
        echo '    }' >> "$TMP"
    fi
done < "$TARGET"

mv "$TMP" "$TARGET"
chmod +x "$TARGET"

echo "[EXPECTANCY] Judge injected"
PATCH

chmod +x expectancy/INJECT_EXPECTANCY_JUDGE.sh

############################################
# BUILD GUARD
############################################
cat >> CONTROL_BUILD.sh << 'HOOK'

############################################
# EXPECTANCY JUDGE CHECK
############################################
[ -f expectancy/ExpectancyJudge.hpp ] || { echo "FATAL: Expectancy layer missing"; exit 1; }
HOOK

############################################
# RUN GUARD
############################################
cat >> CONTROL_RUN.sh << 'HOOK'

############################################
# EXPECTANCY ACTIVE CHECK
############################################
echo "[EXPECTANCY] Active — engines with negative expectancy will be killed"
HOOK

echo "[EXPECTANCY] Installed"
echo "[NEXT]"
echo "  ./expectancy/INJECT_EXPECTANCY_JUDGE.sh"
echo "  ./CONTROL_BUILD.sh"
echo "  ./CONTROL_RUN.sh"
