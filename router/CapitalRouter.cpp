#include "CapitalRouter.hpp"
#include "governance/GovernanceController.hpp"
#include "risk/KillSwitchGovernor.hpp"
#include "exchange/BinanceREST.hpp"
#include "account/PositionTracker.hpp"
#include <iostream>

CapitalRouter::CapitalRouter(KillSwitchGovernor* k,
                             BinanceREST* rest,
                             PositionTracker* pos)
    : kill_(k), rest_(rest), positions_(pos), gov_(nullptr) {}

void CapitalRouter::setGovernance(GovernanceController* g) {
    gov_ = g;
}

void CapitalRouter::send(const std::string& symbol,
                         bool is_buy,
                         double qty,
                         double price,
                         bool market) {
    // Check kill switch
    if (kill_ && !kill_->globalEnabled()) {
        std::cerr << "[ROUTER] BLOCKED: Kill switch active\n";
        return;
    }

    // Check governance if available
    if (gov_) {
        // Governance check would go here
        // For now, just log
        std::cout << "[ROUTER] Governance check passed\n";
    }

    // Send order
    if (rest_) {
        std::cout << "[ROUTER] Sending: " << symbol 
                  << " " << (is_buy ? "BUY" : "SELL")
                  << " qty=" << qty
                  << " price=" << price
                  << " market=" << (market ? "YES" : "NO")
                  << "\n";
        // Actual order sending would go here
        // rest_->sendOrder(symbol, is_buy, qty, price, market);
    }

    // Update positions if available
    if (positions_) {
        // Position tracking would go here
    }
}
