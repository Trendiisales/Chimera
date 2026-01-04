#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// QueueAwarePricer - Avoids bad queue positions
// v4.2.2: Uses IOC when deep queue would cause adverse selection
// ═══════════════════════════════════════════════════════════════════════════

#include <cmath>

namespace Chimera {

enum class ExecStyle {
    JOIN_QUEUE,   // Post limit at best price (normal)
    STEP_IN,      // Post limit inside spread (aggressive)
    IOC           // Immediate-or-cancel (cross spread)
};

struct PriceDecision {
    double price = 0.0;
    ExecStyle style = ExecStyle::JOIN_QUEUE;
};

class QueueAwarePricer {
public:
    // Decide price and execution style based on queue depth
    PriceDecision decide(bool is_buy,
                         double best_bid,
                         double best_ask,
                         double bid_qty,
                         double ask_qty) const
    {
        // Deep queue = bad position = use IOC to cross
        // Ratio threshold: if our side has 2x more qty, we're behind too many orders
        bool deep_queue = is_buy ? (bid_qty > ask_qty * 2.0)
                                 : (ask_qty > bid_qty * 2.0);
        
        // Imbalanced book = adverse selection likely
        double total_qty = bid_qty + ask_qty;
        double imbalance = (total_qty > 0) ? std::abs(bid_qty - ask_qty) / total_qty : 0;
        
        if (deep_queue) {
            // Cross the spread - avoid queue
            return {
                is_buy ? best_ask : best_bid,
                ExecStyle::IOC
            };
        }
        
        if (imbalance > 0.4) {
            // Step into spread slightly to get better queue position
            double spread = best_ask - best_bid;
            double step = spread * 0.2;  // 20% into spread
            return {
                is_buy ? best_bid + step : best_ask - step,
                ExecStyle::STEP_IN
            };
        }
        
        // Normal join at best price
        return {
            is_buy ? best_bid : best_ask,
            ExecStyle::JOIN_QUEUE
        };
    }
    
    // Force IOC (for exits or urgent fills)
    PriceDecision force_ioc(bool is_buy, double best_bid, double best_ask) const {
        return {
            is_buy ? best_ask : best_bid,
            ExecStyle::IOC
        };
    }
    
    // Force join queue (for passive fills)
    PriceDecision force_passive(bool is_buy, double best_bid, double best_ask) const {
        return {
            is_buy ? best_bid : best_ask,
            ExecStyle::JOIN_QUEUE
        };
    }
};

}  // namespace Chimera
