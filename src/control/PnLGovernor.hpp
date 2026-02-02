#pragma once

#include <unordered_map>
#include <string>
#include <mutex>

namespace chimera {

// ---------------------------------------------------------------------------
// Per-strategy and portfolio PnL governor.
//
// Strategies earn the right to trade by maintaining positive rolling EV.
// If rolling EV drops below the configured floor, the strategy is killed:
//   - New orders from that strategy are blocked at submit_order().
//   - In-flight orders are NOT yanked — they fill or cancel through normal
//     lifecycle. Pulling orders mid-queue creates adverse liquidity events
//     and is worse than letting them resolve naturally.
//
// Portfolio level: if total realized PnL drops below the daily loss limit,
//   portfolio_killed() returns true. ExecutionRouter checks this in poll()
//   and fires drift().trigger() to halt everything. This separation keeps
//   PnLGovernor independent of the runtime kill mechanism.
//
// Runs in BOTH shadow and live modes. Shadow fills generate entry execution
// quality estimates (fill price vs mid). This provides realistic per-strategy
// signal before going live and lets kill thresholds be validated in shadow.
//
// Threading: update_fill() is called from CORE1 (ExecutionRouter poll thread).
//   allow_strategy() is called from CORE1 strategy threads (via StrategyRunner).
//   Both acquire mtx_ — no data races. The mutex is uncontended in the common
//   case (allow_strategy checks a map, returns immediately).
// ---------------------------------------------------------------------------
class PnLGovernor {
public:
    struct StrategyStats {
        double realized_pnl{0.0};   // cumulative realized/estimated PnL
        double rolling_ev{0.0};     // exponential moving average of per-fill PnL
        bool   killed{false};       // true once rolling_ev breached floor
    };

    PnLGovernor();

    // ---------------------------------------------------------------------------
    // Configuration — call from main() before trading starts.
    // ---------------------------------------------------------------------------
    void set_strategy_floor(double ev_floor);
    void set_portfolio_dd(double max_loss);

    // ---------------------------------------------------------------------------
    // Fill event — called on each fill (shadow or live).
    // ---------------------------------------------------------------------------
    void update_fill(const std::string& strategy, double pnl_delta);

    // ---------------------------------------------------------------------------
    // Strategy gate — returns false if strategy is killed or portfolio is killed.
    // ---------------------------------------------------------------------------
    bool allow_strategy(const std::string& strategy) const;

    // ---------------------------------------------------------------------------
    // Force-kill an engine by name. Used by EdgeAttribution when an engine
    // is detected leaking edge persistently. Sets killed=true immediately —
    // same effect as EV breaching the floor, but without waiting for PnL
    // decay to get there. One-shot: stays killed until reset().
    // ---------------------------------------------------------------------------
    void block_engine(const std::string& engine_id);

    // ---------------------------------------------------------------------------
    // Portfolio state — for telemetry and drift kill decisions.
    // ---------------------------------------------------------------------------
    double portfolio_pnl() const;
    bool   portfolio_killed() const;

    // Dump all per-strategy stats — for telemetry/console display.
    std::unordered_map<std::string, StrategyStats> dump_stats() const;

    // Reset all state. For testing or manual recovery after operator investigation.
    void reset();

private:
    void update_ev(StrategyStats& stats, double delta);

    double ev_floor_{-10.0};
    double portfolio_dd_{-500.0};

    mutable std::mutex mtx_;
    std::unordered_map<std::string, StrategyStats> stats_;

    double portfolio_pnl_{0.0};
    bool   portfolio_killed_{false};
};

} // namespace chimera
