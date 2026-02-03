#pragma once
#include <vector>
#include <memory>
#include <thread>

#include "exchange/VenueAdapter.hpp"
#include "runtime/Context.hpp"

namespace chimera {

class MultiVenueManager {
public:
    explicit MultiVenueManager(Context& ctx);
    ~MultiVenueManager();

    void add(std::unique_ptr<VenueAdapter> v);

    // FIX 4.2: start() takes core_id — all spawned threads are pinned to that core.
    // Previously: threads were spawned without CPU pinning. They inherited whatever
    // core the OS assigned. The CPU pinning intent (CORE0=feeds, CORE1=execution)
    // was aspirational, not enforced across the thread tree.
    // Now: caller passes the target core_id, all venue market+user threads are pinned.
    void start(int core_id);
    void stop();

    // Periodic live reconciliation — pulls exchange truth
    void reconcile_live();

    // Expose raw adapter pointers for cold-start reconciler
    std::vector<VenueAdapter*> adapters() const;

private:
    Context& ctx_;
    std::vector<std::unique_ptr<VenueAdapter>> venues_;
    std::vector<std::thread> threads_;
};

}
