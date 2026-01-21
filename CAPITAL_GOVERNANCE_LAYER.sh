#!/usr/bin/env bash
set -e

[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }

echo "[CAPITAL] Installing Capital Governance Layer"

mkdir -p governance

############################################
# GOVERNANCE RULES
############################################
cat > governance/CapitalRules.hpp << 'HPP'
#pragma once
#include "regime/MarketRegime.hpp"

struct CapitalLimits {
    double max_leverage;
    double max_alloc;
};

inline CapitalLimits limits_for_regime(MarketRegime r) {
    switch (r) {
        case MarketRegime::COMPRESSION:
            return {2.0, 0.25};
        case MarketRegime::EXPANSION:
            return {3.0, 0.40};
        case MarketRegime::VACUUM:
            return {1.5, 0.15};
        case MarketRegime::ABSORPTION:
            return {1.0, 0.10};
        case MarketRegime::MEAN_REVERT:
            return {2.0, 0.25};
        default:
            return {0.0, 0.0};
    }
}
HPP

############################################
# CAPITAL GOVERNOR
############################################
cat > governance/CapitalGovernor.hpp << 'HPP'
#pragma once
#include <stdexcept>
#include <string>
#include "CapitalRules.hpp"

class CapitalGovernor {
public:
    void enforce(
        const std::string& engine,
        MarketRegime regime,
        double requested_alloc,
        double requested_leverage,
        double& out_alloc,
        double& out_leverage
    ) const {
        CapitalLimits lim = limits_for_regime(regime);

        if (lim.max_alloc == 0.0 || lim.max_leverage == 0.0) {
            throw std::runtime_error(
                "CAPITAL KILL: Regime forbids capital for engine " + engine
            );
        }

        out_alloc = requested_alloc;
        out_leverage = requested_leverage;

        if (out_alloc > lim.max_alloc)
            out_alloc = lim.max_alloc;

        if (out_leverage > lim.max_leverage)
            out_leverage = lim.max_leverage;
    }
};
HPP

############################################
# INJECT INTO SHADOW EXECUTOR
############################################
cat > governance/INJECT_CAPITAL_GOVERNOR.sh << 'PATCH'
#!/usr/bin/env bash
set -e
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }

TARGET="execution/ShadowExecutor.cpp"
TMP=$(mktemp)

echo "[CAPITAL] Wiring Capital Governor into ShadowExecutor"

while IFS= read -r line; do
    echo "$line" >> "$TMP"

    if [[ "$line" == *"#include"* && "$line" == *"EngineRegimeGate.hpp"* ]]; then
        echo '#include "governance/CapitalGovernor.hpp"' >> "$TMP"
        echo 'static CapitalGovernor __capital_governor;' >> "$TMP"
    fi

    if [[ "$line" == *"TelemetryBus::instance().recordTrade"* ]]; then
        echo '    double __alloc = row.alloc;' >> "$TMP"
        echo '    double __lev = row.leverage;' >> "$TMP"
        echo '    __capital_governor.enforce(row.engine, __current_regime, __alloc, __lev, __alloc, __lev);' >> "$TMP"
        echo '    row.alloc = __alloc;' >> "$TMP"
        echo '    row.leverage = __lev;' >> "$TMP"
    fi
done < "$TARGET"

mv "$TMP" "$TARGET"
chmod +x "$TARGET"

echo "[CAPITAL] Governor injected"
PATCH

chmod +x governance/INJECT_CAPITAL_GOVERNOR.sh

############################################
# BUILD GUARD
############################################
cat >> CONTROL_BUILD.sh << 'HOOK'

############################################
# CAPITAL GOVERNANCE CHECK
############################################
[ -f governance/CapitalGovernor.hpp ] || { echo "FATAL: Capital layer missing"; exit 1; }
HOOK

echo "[CAPITAL] Installed"
echo "[NEXT]"
echo "  ./governance/INJECT_CAPITAL_GOVERNOR.sh"
echo "  ./CONTROL_BUILD.sh"
echo "  ./CONTROL_RUN.sh"
