#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>

namespace chimera {

class Context;

// ---------------------------------------------------------------------------
// Desk Arbiter — cross-engine capital governance.
//
// Engines are grouped into desks (e.g. BTC_DESK, ETH_DESK, MEAN_REV_DESK).
// Each desk tracks rolling PnL via EWMA. Per-desk rules:
//
//   Single desk enters loss regime (ewma_pnl < threshold after min_trades):
//     → That desk is PAUSED. Its engines cannot submit.
//     → Other desks continue (they may be in different regimes).
//
//   Desk recovers (ewma_pnl > 0):
//     → Desk is RESUMED. Gradual — no burst of pent-up orders.
//
//   2+ desks paused simultaneously:
//     → REGIME EVENT. This is not a per-desk issue — the market itself is
//       hostile. Cancel Federation fires. All desks frozen until operator
//       intervention or manual reset.
//
// WHY 2-DESK THRESHOLD FOR CANCEL FEDERATION:
//   A single desk losing is normal. BTC drops, BTC desk pauses, ETH/SOL
//   keep trading. This is desk-level risk management.
//   Two desks losing simultaneously = correlated regime shift. BTC and ETH
//   both dropping = market-wide event. The system cannot distinguish "bad
//   strategy" from "bad market" at this point. Stop everything.
//
// Threading: register_engine() called from main() (setup). allow_submit()
//   called from StrategyRunner threads. on_fill() called from CORE1.
//   poll() called from CORE1. All access desk state under no explicit lock —
//   allow_submit() reads paused (bool) which is written by on_fill/poll on
//   CORE1. StrategyRunner threads may see stale paused=false for one tick
//   after a pause — one extra order is not a capital risk. This is acceptable
//   at HFT granularity. If strict ordering is needed, add a mutex.
// ---------------------------------------------------------------------------
class DeskArbiter {
public:
    explicit DeskArbiter(Context& ctx);

    // Register an engine → desk mapping. Called from main() during setup.
    void register_engine(const std::string& engine_id,
                         const std::string& desk_id);

    // Submit gate — returns false if this engine's desk is paused.
    // Unknown engines are allowed (not every engine needs desk governance).
    bool allow_submit(const std::string& engine_id) const;

    // Fill event — update desk PnL. Called from CORE1.
    void on_fill(const std::string& engine_id, double pnl_bps);

    // Poll — check for desk recovery and multi-desk regime events.
    // Called from CORE1 loop.
    void poll();

    // Manual reset — clears all desk state. For operator recovery after
    // a cancel federation event.
    void reset();

private:
    struct DeskState {
        double   ewma_pnl_bps{0.0};
        uint64_t trades{0};
        bool     paused{false};
    };

    Context& ctx_;

    std::unordered_map<std::string, std::string>  engine_to_desk_;  // engine → desk
    std::unordered_map<std::string, DeskState>    desks_;           // desk → state

    double   alpha_{0.05};
    double   loss_threshold_bps_{-5.0};
    uint64_t min_trades_{5};
};

} // namespace chimera
