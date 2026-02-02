#include "control/DeskArbiter.hpp"
#include "runtime/Context.hpp"
#include "execution/CancelFederation.hpp"
#include <iostream>

using namespace chimera;

DeskArbiter::DeskArbiter(Context& ctx)
    : ctx_(ctx) {}

void DeskArbiter::register_engine(const std::string& engine_id,
                                   const std::string& desk_id) {
    engine_to_desk_[engine_id] = desk_id;
    desks_.try_emplace(desk_id, DeskState{});
}

bool DeskArbiter::allow_submit(const std::string& engine_id) const {
    auto it = engine_to_desk_.find(engine_id);
    if (it == engine_to_desk_.end()) return true;  // unregistered = allowed

    auto dit = desks_.find(it->second);
    if (dit == desks_.end()) return true;
    return !dit->second.paused;
}

void DeskArbiter::on_fill(const std::string& engine_id, double pnl_bps) {
    auto it = engine_to_desk_.find(engine_id);
    if (it == engine_to_desk_.end()) return;

    DeskState& d = desks_[it->second];

    d.ewma_pnl_bps = (1.0 - alpha_) * d.ewma_pnl_bps + alpha_ * pnl_bps;
    d.trades++;

    // ---------------------------------------------------------------------------
    // Single-desk loss: pause THIS desk only. Other desks continue.
    // This is per-desk risk management, not a regime event.
    // ---------------------------------------------------------------------------
    if (d.trades >= min_trades_ &&
        d.ewma_pnl_bps < loss_threshold_bps_ &&
        !d.paused)
    {
        d.paused = true;
        std::cerr << "[DESK] PAUSED " << it->second
                  << " ewma_pnl=" << d.ewma_pnl_bps
                  << " trades=" << d.trades << "\n";

        // NOTE: we do NOT fire cancel_fed here. One desk pausing is normal.
        // The multi-desk regime check runs in poll().
    }
}

void DeskArbiter::poll() {
    // ---------------------------------------------------------------------------
    // 1. Recovery check: if a desk's ewma has recovered to positive, resume it.
    // ---------------------------------------------------------------------------
    for (auto& [desk_id, d] : desks_) {
        if (!d.paused) continue;

        if (d.ewma_pnl_bps > 0.0) {
            d.paused = false;
            std::cerr << "[DESK] RESUMED " << desk_id
                      << " ewma_pnl=" << d.ewma_pnl_bps << "\n";
        }
    }

    // ---------------------------------------------------------------------------
    // 2. Multi-desk regime check: if 2+ desks are paused simultaneously,
    //    this is a correlated regime event. Cancel Federation fires.
    //    All desks stay paused — operator must reset() to resume.
    //
    //    This check runs AFTER recovery so desks that just recovered
    //    don't falsely trigger. Only truly-still-losing desks count.
    // ---------------------------------------------------------------------------
    int paused_count = 0;
    for (const auto& [desk_id, d] : desks_) {
        if (d.paused) paused_count++;
    }

    if (paused_count >= 2) {
        std::cerr << "[DESK] REGIME EVENT: " << paused_count
                  << " desks paused simultaneously — CANCEL FEDERATION\n";
        ctx_.cancel_fed.trigger("DESK_REGIME");
    }
}

void DeskArbiter::reset() {
    for (auto& [desk_id, d] : desks_) {
        d.ewma_pnl_bps = 0.0;
        d.trades = 0;
        d.paused = false;
    }
    std::cout << "[DESK] All desks reset\n";
}
