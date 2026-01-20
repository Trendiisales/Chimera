#include "../telemetry/TelemetryBus.hpp"
#include "CapitalRouter.hpp"
#include "../risk/KillSwitchGovernor.hpp"
#include "../exchange/BinanceREST.hpp"
#include "../account/PositionTracker.hpp"

#include <iostream>

CapitalRouter::CapitalRouter(KillSwitchGovernor* k,
                             BinanceREST* rest,
                             PositionTracker* pos)
    : kill_(k),
      rest_(rest),
      positions_(pos) {}

void CapitalRouter::send(const std::string& symbol,
                          bool is_buy,
                          double qty,
                          double price,
                          bool market) {
    if (!kill_ || !kill_->globalEnabled()) {
        std::cout << "[ROUTER] BLOCKED BY KILL SWITCH\n";
        return;
    }

    if (!rest_) {
        std::cout << "[ROUTER] NO REST CLIENT\n";
        return;
    }

    const std::string side = is_buy ? "BUY" : "SELL";

    std::string resp = rest_->sendOrder(
        symbol,
        side,
        qty,
        price,
        market
    );

    if (positions_) {
        positions_->onFill(symbol, is_buy ? qty : -qty, price);
    }

    std::cout << "[ROUTER] "
              << symbol
              << " "
              << side
              << " qty=" << qty
              << " price=" << price
              << " resp=" << resp
              << "\n";
}
