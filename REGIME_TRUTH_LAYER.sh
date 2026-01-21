#!/usr/bin/env bash
set -e

[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }

echo "[REGIME] Installing Regime Truth Layer"

mkdir -p regime

############################################
# REGIME TYPES
############################################
cat > regime/MarketRegime.hpp << 'HPP'
#pragma once
#include <string>

enum class MarketRegime {
    COMPRESSION,
    EXPANSION,
    VACUUM,
    ABSORPTION,
    MEAN_REVERT,
    UNKNOWN
};

inline const char* to_string(MarketRegime r) {
    switch (r) {
        case MarketRegime::COMPRESSION: return "COMPRESSION";
        case MarketRegime::EXPANSION: return "EXPANSION";
        case MarketRegime::VACUUM: return "VACUUM";
        case MarketRegime::ABSORPTION: return "ABSORPTION";
        case MarketRegime::MEAN_REVERT: return "MEAN_REVERT";
        default: return "UNKNOWN";
    }
}
HPP

############################################
# REGIME CLASSIFIER (MINIMAL / SAFE)
############################################
cat > regime/RegimeClassifier.hpp << 'HPP'
#pragma once
#include "MarketRegime.hpp"

struct RegimeInputs {
    double spread_bps;
    double ofi_accel;
    double volatility_bps;
};

class RegimeClassifier {
public:
    MarketRegime classify(const RegimeInputs& in) const {
        if (in.spread_bps > 8.0 && in.ofi_accel > 12.0)
            return MarketRegime::VACUUM;

        if (in.volatility_bps > 15.0 && in.ofi_accel > 8.0)
            return MarketRegime::EXPANSION;

        if (in.volatility_bps < 4.0 && in.spread_bps < 2.0)
            return MarketRegime::COMPRESSION;

        if (in.ofi_accel < -6.0)
            return MarketRegime::MEAN_REVERT;

        return MarketRegime::UNKNOWN;
    }
};
HPP

############################################
# ENGINE GATE
############################################
cat > regime/EngineRegimeGate.hpp << 'HPP'
#pragma once
#include <string>
#include <stdexcept>
#include "MarketRegime.hpp"

inline void enforce_engine_regime(
    const std::string& engine,
    MarketRegime regime
) {
    bool allowed = false;

    if (engine == "FADE") {
        allowed = (regime == MarketRegime::COMPRESSION ||
                   regime == MarketRegime::MEAN_REVERT ||
                   regime == MarketRegime::ABSORPTION);
    }

    if (engine == "CASCADE") {
        allowed = (regime == MarketRegime::EXPANSION ||
                   regime == MarketRegime::VACUUM);
    }

    if (engine == "MOMENTUM") {
        allowed = (regime == MarketRegime::EXPANSION);
    }

    if (!allowed) {
        throw std::runtime_error(
            "REGIME VIOLATION: Engine '" + engine +
            "' not allowed in regime " + to_string(regime)
        );
    }
}
HPP

############################################
# INJECT INTO SHADOW EXECUTOR (NO SED)
############################################
cat > regime/INJECT_REGIME_GUARD.sh << 'PATCH'
#!/usr/bin/env bash
set -e
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }

TARGET="execution/ShadowExecutor.cpp"
TMP=$(mktemp)

echo "[REGIME] Wiring regime gate into ShadowExecutor"

while IFS= read -r line; do
    echo "$line" >> "$TMP"

    if [[ "$line" == *"#include"* && "$line" == *"ShadowExecutor.hpp"* ]]; then
        echo '#include "regime/RegimeClassifier.hpp"' >> "$TMP"
        echo '#include "regime/EngineRegimeGate.hpp"' >> "$TMP"
        echo 'static RegimeClassifier __regime_classifier;' >> "$TMP"
        echo 'static MarketRegime __current_regime = MarketRegime::UNKNOWN;' >> "$TMP"
    fi

    if [[ "$line" == *"onTick("* && "$line" == *"TickData"* ]]; then
        echo '    RegimeInputs __ri{spread_bps_, ofi_accel_, volatility_bps_};' >> "$TMP"
        echo '    __current_regime = __regime_classifier.classify(__ri);' >> "$TMP"
    fi

    if [[ "$line" == *"TelemetryBus::instance().recordTrade"* ]]; then
        echo '    enforce_engine_regime(row.engine, __current_regime);' >> "$TMP"
    fi
done < "$TARGET"

mv "$TMP" "$TARGET"
chmod +x "$TARGET"

echo "[REGIME] Guard injected"
PATCH

chmod +x regime/INJECT_REGIME_GUARD.sh

############################################
# BUILD GUARD
############################################
cat >> CONTROL_BUILD.sh << 'HOOK'

############################################
# REGIME TRUTH CHECK
############################################
[ -f regime/MarketRegime.hpp ] || { echo "FATAL: Regime layer missing"; exit 1; }
HOOK

echo "[REGIME] Installed"
echo "[NEXT]"
echo "  ./regime/INJECT_REGIME_GUARD.sh"
echo "  ./CONTROL_BUILD.sh"
echo "  ./CONTROL_RUN.sh"
