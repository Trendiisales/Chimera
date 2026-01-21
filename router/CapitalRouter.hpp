#pragma once
#include <string>

class KillSwitchGovernor;
class BinanceREST;
class PositionTracker;
class GovernanceController;

class CapitalRouter {
public:
    CapitalRouter(KillSwitchGovernor* k,
                  BinanceREST* rest,
                  PositionTracker* pos);

    void setGovernance(GovernanceController* g);

    void send(const std::string& symbol,
              bool is_buy,
              double qty,
              double price,
              bool market);

private:
    KillSwitchGovernor* kill_;
    BinanceREST* rest_;
    PositionTracker* positions_;
    GovernanceController* gov_;
};
