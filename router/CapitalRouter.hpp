#pragma once
#include <string>

class KillSwitchGovernor;
class BinanceREST;
class PositionTracker;

class CapitalRouter {
    KillSwitchGovernor* kill_;
    BinanceREST* rest_;
    PositionTracker* positions_;

public:
    CapitalRouter(KillSwitchGovernor* k,
                   BinanceREST* rest,
                   PositionTracker* pos);

    void send(const std::string& symbol,
              bool is_buy,
              double qty,
              double price,
              bool market);
};
