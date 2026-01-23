#pragma once

#include <string>

namespace chimera {

class SessionGovernor;
class RegimeClassifier;
class CapitalLadder;

struct GovernanceSnapshot {
    int regime_quality;
    double ladder_tier;
    bool eth_locked;
    bool kill_enabled = true;  // Kill switch status
};

class GovernanceController {
public:
    GovernanceController(
        SessionGovernor* session,
        RegimeClassifier* regime,
        CapitalLadder* ladder
    );

    bool allowTrade(
        const std::string& symbol,
        bool is_buy,
        double edge_score,
        double cost_bps
    ) const;

    GovernanceSnapshot snapshot() const;

private:
    SessionGovernor* session_;
    RegimeClassifier* regime_;
    CapitalLadder* ladder_;
};

} // namespace chimera
