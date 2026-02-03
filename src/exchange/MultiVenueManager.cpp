#include "exchange/MultiVenueManager.hpp"
#include "runtime/CpuPinning.hpp"
#include <iostream>

using namespace chimera;

MultiVenueManager::MultiVenueManager(Context& ctx)
    : ctx_(ctx) {}

MultiVenueManager::~MultiVenueManager() {
    stop();
}

void MultiVenueManager::add(std::unique_ptr<VenueAdapter> v) {
    venues_.push_back(std::move(v));
}

// FIX 4.2: All spawned threads are pinned to core_id.
// Previously: threads were created with no affinity. They floated on whatever
// cores the scheduler chose. Now they're pinned to the same core as the caller
// intended (typically CORE0 for all feed threads).
void MultiVenueManager::start(int core_id) {
    for (auto& v : venues_) {
        threads_.emplace_back([this, vptr = v.get(), cid = core_id]() {
            CpuPinning::pin_thread(cid);
            vptr->run_market(ctx_.running);
        });
        threads_.emplace_back([this, vptr = v.get(), cid = core_id]() {
            CpuPinning::pin_thread(cid);
            vptr->run_user(ctx_.running);
        });
    }
}

void MultiVenueManager::stop() {
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();
}

// B8 FIX: uses get_all_positions() — matches updated VenueAdapter interface.
// Pushes every venue's positions into exchange-truth reconciler.
void MultiVenueManager::reconcile_live() {
    for (auto& v : venues_) {
        std::vector<VenuePosition> positions;
        if (!v->get_all_positions(positions)) {
            std::cout << "[RECON] " << v->name() << " position pull failed\n";
            continue;
        }
        for (const auto& pos : positions) {
            ctx_.risk.reconciler().on_exchange_position(
                {pos.symbol, pos.qty, pos.entry_price});
        }
    }
}

std::vector<VenueAdapter*> MultiVenueManager::adapters() const {
    std::vector<VenueAdapter*> out;
    out.reserve(venues_.size());
    for (auto& v : venues_)
        out.push_back(v.get());
    return out;
}
