#pragma once
#include <string>
#include "exchange/binance/BinanceRestClient.hpp"

namespace chimera {

// Binance cold-start reconciler.
// Queries /api/v3/account and /api/v3/openOrders via REST.
// Returns true ONLY if:
//   - All positions have positionAmt == 0
//   - No open orders exist
// If any dirty state is detected, reconcile() returns false and
// the arm gate blocks. report() contains the human-readable findings.
class BinanceReconciler {
public:
    explicit BinanceReconciler(BinanceRestClient& rest);

    // Query exchange truth. Returns true if clean (safe to arm).
    bool reconcile();

    // Human-readable reconciliation report — valid after reconcile() call.
    const std::string& report() const;

private:
    BinanceRestClient& rest_;
    std::string report_;
};

}
