#!/usr/bin/env bash
set -e

[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }

echo "[DRAWDOWN] Installing Drawdown Sentinel Layer"

mkdir -p drawdown

############################################
# DRAWDOWN ENGINE
############################################
cat > drawdown/DrawdownSentinel.hpp << 'HPP'
#pragma once
#include <unordered_map>
#include <string>
#include <cmath>

struct DrawdownStats {
    double peak_bps = 0.0;
    double trough_bps = 0.0;
    double drawdown_bps = 0.0;
};

class DrawdownSentinel {
public:
    explicit DrawdownSentinel(double max_dd_bps = 20.0)
        : max_dd_bps_(max_dd_bps) {}

    void update(const std::string& engine, double pnl_bps) {
        auto& s = stats_[engine];

        if (!initialized_[engine]) {
            s.peak_bps = pnl_bps;
            s.trough_bps = pnl_bps;
            initialized_[engine] = true;
            return;
        }

        if (pnl_bps > s.peak_bps)
            s.peak_bps = pnl_bps;

        if (pnl_bps < s.trough_bps)
            s.trough_bps = pnl_bps;

        s.drawdown_bps = s.peak_bps - s.trough_bps;
    }

    bool allowed(const std::string& engine) const {
        auto it = stats_.find(engine);
        if (it == stats_.end())
            return true;
        return it->second.drawdown_bps <= max_dd_bps_;
    }

    DrawdownStats stats(const std::string& engine) const {
        auto it = stats_.find(engine);
        if (it == stats_.end())
            return {};
        return it->second;
    }

private:
    double max_dd_bps_;
    std::unordered_map<std::string, DrawdownStats> stats_;
    std::unordered_map<std::string, bool> initialized_;
};
HPP

############################################
# INJECT INTO SHADOW EXECUTOR
############################################
cat > drawdown/INJECT_DRAWDOWN_SENTINEL.sh << 'PATCH'
#!/usr/bin/env bash
set -e
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }

TARGET="execution/ShadowExecutor.cpp"
TMP=$(mktemp)

echo "[DRAWDOWN] Wiring Drawdown Sentinel into ShadowExecutor"

while IFS= read -r line; do
    echo "$line" >> "$TMP"

    if [[ "$line" == *"#include"* && "$line" == *"ExpectancyJudge.hpp"* ]]; then
        echo '#include "drawdown/DrawdownSentinel.hpp"' >> "$TMP"
        echo 'static DrawdownSentinel __drawdown_sentinel(20.0);' >> "$TMP"
    fi

    if [[ "$line" == *"__expectancy_judge.record"* ]]; then
        echo '    __drawdown_sentinel.update(row.engine, row.bps);' >> "$TMP"
        echo '    if (!__drawdown_sentinel.allowed(row.engine)) {' >> "$TMP"
        echo '        throw std::runtime_error("DRAWDOWN KILL: Engine " + row.engine + " exceeded drawdown");' >> "$TMP"
        echo '    }' >> "$TMP"
    fi
done < "$TARGET"

mv "$TMP" "$TARGET"
chmod +x "$TARGET"

echo "[DRAWDOWN] Sentinel injected"
PATCH

chmod +x drawdown/INJECT_DRAWDOWN_SENTINEL.sh

############################################
# BUILD GUARD
############################################
cat >> CONTROL_BUILD.sh << 'HOOK'

############################################
# DRAWDOWN SENTINEL CHECK
############################################
[ -f drawdown/DrawdownSentinel.hpp ] || { echo "FATAL: Drawdown layer missing"; exit 1; }
HOOK

############################################
# RUN GUARD
############################################
cat >> CONTROL_RUN.sh << 'HOOK'

############################################
# DRAWDOWN ACTIVE CHECK
############################################
echo "[DRAWDOWN] Active — engines exceeding drawdown will be force-killed"
HOOK

echo "[DRAWDOWN] Installed"
echo "[NEXT]"
echo "  ./drawdown/INJECT_DRAWDOWN_SENTINEL.sh"
echo "  ./CONTROL_BUILD.sh"
echo "  ./CONTROL_RUN.sh"
