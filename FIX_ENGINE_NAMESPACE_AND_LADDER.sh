#!/usr/bin/env bash
set -e

echo "[CHIMERA] Normalizing engine/governance boundaries"

########################################
# ENGINE CAPITAL LADDER ADAPTER
########################################

cat > core/include/engines/CapitalLadder.hpp << 'EOC'
#pragma once

#include <cstdint>

namespace chimera {
namespace governance {
class CapitalLadder;
}
namespace engines {

class CapitalLadderAdapter {
public:
    explicit CapitalLadderAdapter(governance::CapitalLadder* gov);

    void recordWin();
    void recordLoss();

    double sizeMultiplier() const;
    void applyDrawdown(double dd_bps);

private:
    governance::CapitalLadder* gov_;
};

} // namespace engines
} // namespace chimera
EOC

cat > core/src/engines/CapitalLadder.cpp << 'EOC'
#include "engines/CapitalLadder.hpp"
#include "governance/CapitalLadder.hpp"

namespace chimera {
namespace engines {

CapitalLadderAdapter::CapitalLadderAdapter(governance::CapitalLadder* gov)
    : gov_(gov) {}

void CapitalLadderAdapter::recordWin() {
    if (gov_) gov_->recordWin();
}

void CapitalLadderAdapter::recordLoss() {
    if (gov_) gov_->recordLoss();
}

double CapitalLadderAdapter::sizeMultiplier() const {
    return gov_ ? gov_->sizeMultiplier() : 0.0;
}

void CapitalLadderAdapter::applyDrawdown(double dd_bps) {
    if (gov_) gov_->applyDrawdown(dd_bps);
}

} // namespace engines
} // namespace chimera
EOC

########################################
# FUNDING SNIPER FIX
########################################

cat > core/src/engines/FundingSniper.cpp << 'EOC'
#include "engines/FundingSniper.hpp"

namespace chimera {
namespace engines {

FundingSniper::FundingSniper()
    : funding_rate_(0.0),
      next_funding_ts_us_(0) {}

void FundingSniper::update(double funding_rate, uint64_t next_funding_ts_us) {
    funding_rate_ = funding_rate;
    next_funding_ts_us_ = next_funding_ts_us;
}

bool FundingSniper::shouldFire(uint64_t now_us) const {
    if (next_funding_ts_us_ == 0) return false;
    uint64_t diff = (next_funding_ts_us_ > now_us)
        ? (next_funding_ts_us_ - now_us)
        : 0;
    return diff < 30ULL * 1000ULL * 1000ULL;
}

bool FundingSniper::isBuy() const {
    return funding_rate_ > 0.0;
}

double FundingSniper::sizeBias() const {
    double r = funding_rate_;
    if (r < 0) r = -r;
    return 1.0 + r * 10.0;
}

} // namespace engines
} // namespace chimera
EOC

########################################
# REGIME CLASSIFIER FIX
########################################

cat > core/src/engines/RegimeClassifier.cpp << 'EOC'
#include "engines/RegimeClassifier.hpp"

namespace chimera {
namespace engines {

RegimeClassifier::RegimeClassifier()
    : quality_(0) {}

void RegimeClassifier::update(int q) {
    quality_ = q;
}

int RegimeClassifier::quality() const {
    return quality_;
}

} // namespace engines
} // namespace chimera
EOC

########################################
# GOVERNANCE CONTROLLER INCLUDE NORMALIZATION
########################################

cat > core/src/governance/GovernanceController.cpp << 'EOC'
#include "governance/GovernanceController.hpp"
#include "governance/SessionGovernor.hpp"
#include "governance/RegimeClassifier.hpp"
#include "governance/CapitalLadder.hpp"

namespace chimera {

GovernanceController::GovernanceController(SessionGovernor* s,
                                           RegimeClassifier* r,
                                           CapitalLadder* c)
    : session_(s), regime_(r), ladder_(c) {}

bool GovernanceController::allowTrade(const std::string& symbol,
                                      bool,
                                      double edge_bps,
                                      double cost_bps) const {
    if (!session_ || !regime_ || !ladder_) return false;

    if (!session_->tradingAllowed()) return false;
    if (session_->ethLocked() && symbol == "ETH") return false;

    if (regime_->quality() < 1) return false;
    if (ladder_->sizeMultiplier() <= 0.0) return false;

    return (edge_bps - cost_bps) > 0.0;
}

GovernanceSnapshot GovernanceController::snapshot() const {
    GovernanceSnapshot g{};
    g.regime_quality = regime_ ? regime_->quality() : 0;
    g.ladder_tier   = ladder_ ? ladder_->sizeMultiplier() : 0.0;
    g.eth_locked   = session_ ? session_->ethLocked() : false;
    return g;
}

} // namespace chimera
EOC

########################################
# WIPE BUILD + REBUILD
########################################

rm -rf build
mkdir build
cd build
cmake ..
make -j2

echo "[CHIMERA] ENGINE / GOVERNANCE BOUNDARY FIXED"
