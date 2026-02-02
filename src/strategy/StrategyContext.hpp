#pragma once
#include <string>
#include <atomic>
#include "runtime/Context.hpp"
#include "execution/ExecutionRouter.hpp"
#include "execution/QueuePositionModel.hpp"

namespace chimera {

// ---------------------------------------------------------------------------
// Bridge between strategy engines and the institutional execution spine.
//
// Engines do NOT touch Context, ExecutionRouter, or QueuePositionModel directly.
// All market reads and order submissions go through this single interface.
// This keeps engines portable, testable, and decoupled from the runtime.
// ---------------------------------------------------------------------------
class StrategyContext {
public:
    StrategyContext(Context& ctx, ExecutionRouter& router)
        : ctx_(ctx), router_(router) {}

    // ---------------------------------------------------------------------------
    // MARKET DATA — returns current top-of-book for a symbol.
    // Returns false if no data has arrived yet (engine should skip tick).
    // ---------------------------------------------------------------------------
    bool top(const std::string& sym, double& bid, double& ask,
             double& bid_size, double& ask_size) const {
        TopOfBook tb = ctx_.queue.top(sym);
        if (!tb.valid) return false;
        bid      = tb.bid;
        ask      = tb.ask;
        bid_size = tb.bid_size;
        ask_size = tb.ask_size;
        return true;
    }

    // ---------------------------------------------------------------------------
    // EXECUTION — submit an order into ExecutionRouter.
    // client_id must be unique per order (caller generates).
    // qty is signed: positive = buy, negative = sell.
    // engine_id identifies the originating strategy (used by PnLGovernor).
    // Returns true if order entered the pipeline, false if risk/throttle blocked.
    // ---------------------------------------------------------------------------
    bool submit(const std::string& client_id,
                const std::string& sym,
                double price,
                double qty,
                const std::string& engine_id) {
        return router_.submit_order(client_id, sym, price, qty, engine_id);
    }

    // ---------------------------------------------------------------------------
    // RISK GATE — pre-check before constructing an order.
    // Engines can call this optionally to avoid wasted work on blocked symbols.
    // qty is signed (positive=buy, negative=sell). pre_check uses fabs internally.
    // ---------------------------------------------------------------------------
    bool allow(const std::string& sym, double price, double qty) const {
        return ctx_.risk.pre_check(sym, price, std::abs(qty));
    }

    // ---------------------------------------------------------------------------
    // SYSTEM KILL CHECK — returns true if drift kill has fired.
    // All engines must stop immediately. This is fatal — operator intervention
    // required to restart.
    // ---------------------------------------------------------------------------
    bool system_killed() const {
        return ctx_.risk.killed();
    }

    // ---------------------------------------------------------------------------
    // ENGINE KILL CHECK — returns true if this engine has been killed by
    // PnLGovernor or EdgeAttribution. StrategyRunner checks this before
    // calling onTick() — a killed engine should not generate intents at all.
    // ---------------------------------------------------------------------------
    bool engine_killed(const std::string& engine_id) const {
        return !ctx_.pnl.allow_strategy(engine_id);
    }

    // ---------------------------------------------------------------------------
    // ARM STATE — returns true if live capital is enabled.
    // Used by StrategyRunner to gate risk pre-checks: in shadow mode risk
    // enforcement happens only at ExecutionRouter (queue probability is the
    // sole gate). Calling pre_check here in shadow mode would duplicate the
    // block and create an infinite submit→block loop since shadow positions
    // accumulate against ceilings with no reset.
    // ---------------------------------------------------------------------------
    bool is_live() const {
        return ctx_.arm.live_enabled();
    }

    // ---------------------------------------------------------------------------
    // POSITION — current net position for a symbol.
    // Injected into MarketTick by StrategyRunner so engines can cap size.
    // ---------------------------------------------------------------------------
    double get_position(const std::string& sym) const {
        return ctx_.risk.get_position(sym);
    }

private:
    Context&         ctx_;
    ExecutionRouter& router_;
};

} // namespace chimera
