#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// RepostGuard - Detects when to cancel and repost orders
// v4.2.2: Anti-adverse selection - cancel if book moves against us
// ═══════════════════════════════════════════════════════════════════════════

#include <cmath>

namespace Chimera {

class RepostGuard {
public:
    // Should we cancel and repost because book moved against us?
    bool should_repost(double last_price,
                       double new_best_price,
                       bool is_buy) const
    {
        if (is_buy) {
            // For buys: if best bid improved, we should repost higher
            return new_best_price > last_price;
        } else {
            // For sells: if best ask improved, we should repost lower
            return new_best_price < last_price;
        }
    }
    
    // How much has price moved (for urgency detection)
    double price_drift(double last_price, double new_price) const {
        if (last_price == 0) return 0;
        return (new_price - last_price) / last_price * 10000.0;  // bps
    }
    
    // Should we urgently cancel (price moved significantly)?
    bool urgent_cancel(double last_price,
                       double new_best_price,
                       bool is_buy,
                       double threshold_bps = 1.0) const
    {
        double drift = price_drift(last_price, new_best_price);
        
        if (is_buy) {
            // For buys: urgent if price ran up significantly
            return drift > threshold_bps;
        } else {
            // For sells: urgent if price dropped significantly
            return drift < -threshold_bps;
        }
    }
    
    // Should we let the order rest (price stable)?
    bool should_rest(double last_price, double new_best_price) const {
        double drift = std::abs(price_drift(last_price, new_best_price));
        return drift < 0.3;  // Less than 0.3 bps movement
    }
};

}  // namespace Chimera
