#pragma once
#include "execution/Tier1ExecutionRouter.hpp"
#include "tier2/ElasticCapital.hpp"
#include "tier2/FillToxicityFilter.hpp"
#include "tier2/LatencyEVGate.hpp"
#include "tier2/MakerQueueHealth.hpp"

namespace chimera {

// ---------------------------------------------------------------------------
// Tier2ExecutionRouter: Tier1 + Revenue Defense
// 
// Adds to Tier1:
//   - ElasticCapital: Dynamic caps based on PnL
//   - FillToxicity: Detect and block adverse selection
//   - LatencyEVGate: Block stale signals
//   - MakerQueueHealth: Cancel aged orders
// 
// Expected gains over Tier1:
//   - +15-25% capital efficiency (elastic caps)
//   - +2-5 bps per trade (toxicity filter)
//   - +1-3 bps (latency gate)
//   - Better fill quality (queue health)
// ---------------------------------------------------------------------------

class Tier2ExecutionRouter : public Tier1ExecutionRouter {
public:
    Tier2ExecutionRouter(Context& ctx, AtomicPositionGate& gate)
        : Tier1ExecutionRouter(ctx, gate) {}

    // Enhanced submission with Tier2 filters
    bool submit_order_tier2(const std::string& client_id,
                            const std::string& symbol,
                            double price, double qty,
                            double edge_bps,
                            const std::string& engine_id) {
        
        // === TIER 2 FILTERS ===
        
        // Filter 1: Latency EV Gate
        if (!ev_gate_.allow(symbol, edge_bps)) {
            // Edge too small for latency
            return false;
        }

        // Filter 2: Fill Toxicity
        if (!toxicity_.allow(symbol)) {
            // Symbol is toxic - block temporarily
            return false;
        }

        // Update queue health tracking
        maker_health_.on_submit(client_id);

        // === TIER 1 SUBMISSION ===
        return submit_order(client_id, symbol, price, qty, engine_id);
    }

    // Update elastic caps based on PnL
    void on_pnl(const std::string& symbol, double pnl_dollars) {
        elastic_.on_pnl(symbol, pnl_dollars);
        
        // Update position gate cap
        double new_cap = elastic_.cap(symbol);
        set_cap(symbol, new_cap);
    }

    // Record fill for toxicity tracking
    void on_fill(const std::string& symbol, double signed_edge_bps) {
        toxicity_.on_fill(symbol, signed_edge_bps);
    }

    // Check for stale maker orders
    bool is_stale(const std::string& order_id) const {
        return maker_health_.stale(order_id);
    }

    // Initialize Tier2 for a symbol
    void init_symbol_tier2(const std::string& symbol, 
                           double base_cap,
                           double latency_ms = 2.0) {
        elastic_.set_base_cap(symbol, base_cap);
        ev_gate_.set_latency_ms(symbol, latency_ms);
        set_cap(symbol, base_cap);
    }

    // Accessors
    ElasticCapital& elastic() { return elastic_; }
    FillToxicityFilter& toxicity() { return toxicity_; }
    LatencyEVGate& ev_gate() { return ev_gate_; }
    MakerQueueHealth& maker_health() { return maker_health_; }

private:
    ElasticCapital elastic_;
    FillToxicityFilter toxicity_;
    LatencyEVGate ev_gate_;
    MakerQueueHealth maker_health_;
};

} // namespace chimera
