#include "governance/GovernanceController.hpp"
#include "governance/SessionGovernor.hpp"
#include "governance/RegimeClassifier.hpp"
#include "governance/CapitalLadder.hpp"
#include "governance/SessionGovernor.hpp"
#include "governance/RegimeClassifier.hpp"
#include "governance/CapitalLadder.hpp"
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
