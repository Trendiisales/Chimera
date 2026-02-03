#pragma once
#include <cmath>
#include <string>
#include <unordered_map>

namespace chimera {

/**
 * Avellaneda-Stoikov Market Making Model
 * 
 * Dynamically prices bid/ask based on inventory risk.
 * Used by Jump Trading, Jane Street, Wintermute, etc.
 * 
 * Key idea: When long, widen ask and tighten bid to encourage selling.
 *           When short, widen bid and tighten ask to encourage buying.
 * 
 * Formula:
 *   reservation_price = mid - q * γ * σ² * (T - t)
 *   bid = reservation_price - δ/2 + log(1 + γ/κ) / γ
 *   ask = reservation_price + δ/2 - log(1 + γ/κ) / γ
 * 
 * Where:
 *   q = current inventory position
 *   γ = inventory risk aversion (higher = more aggressive inventory management)
 *   σ = volatility
 *   T = time horizon
 *   t = current time
 *   δ = spread
 *   κ = order arrival rate
 */
class AvellanedaStoikov {
public:
    struct Params {
        double gamma = 0.1;       // Risk aversion (0.05-0.3 typical)
        double kappa = 1.5;       // Order arrival rate (trades/second)
        double T = 1.0;           // Time horizon in seconds (0.5-5.0)
        double min_spread_bps = 0.3;  // Minimum spread to quote
        double max_spread_bps = 5.0;  // Maximum spread (risk control)
    };

    AvellanedaStoikov() = default;

    void set_params(const std::string& symbol, const Params& p) {
        params_[symbol] = p;
    }

    /**
     * Compute optimal bid/ask prices given current state
     * 
     * @param symbol Trading symbol
     * @param mid Current mid price
     * @param position Current inventory (>0 long, <0 short)
     * @param volatility Recent volatility (daily σ)
     * @param time_remaining Time until end of trading window (seconds)
     * @param current_spread Current market spread
     * @return {optimal_bid, optimal_ask}
     */
    std::pair<double, double> compute_quotes(
        const std::string& symbol,
        double mid,
        double position,
        double volatility,
        double time_remaining,
        double current_spread
    ) {
        auto it = params_.find(symbol);
        const Params& p = (it != params_.end()) ? it->second : default_params_;

        // Normalize time remaining (0 to 1)
        double time_factor = std::min(time_remaining / p.T, 1.0);

        // Reservation price: mid adjusted for inventory risk
        // When long (q > 0), reservation price < mid (want to sell)
        // When short (q < 0), reservation price > mid (want to buy)
        double q_normalized = position;  // Already in contract units
        double reservation_price = mid - q_normalized * p.gamma * volatility * volatility * time_factor;

        // Spread adjustment based on inventory
        // When at risk aversion limit, widen spread
        double spread_adjustment = std::log(1.0 + p.gamma / p.kappa) / p.gamma;

        // Base spread (use current market spread as reference)
        double base_spread = current_spread;
        
        // Ensure minimum spread
        double min_spread = (p.min_spread_bps / 10000.0) * mid;
        base_spread = std::max(base_spread, min_spread);

        // Ensure maximum spread
        double max_spread = (p.max_spread_bps / 10000.0) * mid;
        base_spread = std::min(base_spread, max_spread);

        // Compute bid/ask around reservation price
        double half_spread = base_spread / 2.0;
        double bid = reservation_price - half_spread + spread_adjustment;
        double ask = reservation_price + half_spread - spread_adjustment;

        // Sanity checks
        bid = std::max(bid, mid * 0.95);  // Don't quote more than 5% away
        ask = std::min(ask, mid * 1.05);
        
        // Ensure bid < ask
        if (bid >= ask) {
            bid = mid - half_spread;
            ask = mid + half_spread;
        }

        return {bid, ask};
    }

    /**
     * Simpler interface: compute bid/ask offsets from mid
     * Returns offsets in basis points
     */
    std::pair<double, double> compute_offsets_bps(
        const std::string& symbol,
        double mid,
        double position,
        double volatility,
        double time_remaining,
        double current_spread_bps
    ) {
        double current_spread = (current_spread_bps / 10000.0) * mid;
        auto [bid, ask] = compute_quotes(symbol, mid, position, volatility, 
                                         time_remaining, current_spread);
        
        double bid_offset_bps = ((mid - bid) / mid) * 10000.0;
        double ask_offset_bps = ((ask - mid) / mid) * 10000.0;
        
        return {bid_offset_bps, ask_offset_bps};
    }

    /**
     * Get expected PnL from inventory position
     * Useful for risk monitoring
     */
    double inventory_risk_pnl(
        const std::string& symbol,
        double position,
        double volatility,
        double time_remaining
    ) {
        auto it = params_.find(symbol);
        const Params& p = (it != params_.end()) ? it->second : default_params_;
        
        double time_factor = std::min(time_remaining / p.T, 1.0);
        double risk = position * position * p.gamma * volatility * volatility * time_factor;
        
        return -risk;  // Negative because it's a cost
    }

private:
    std::unordered_map<std::string, Params> params_;
    Params default_params_;
};

} // namespace chimera
