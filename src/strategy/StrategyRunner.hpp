#pragma once
#include <string>
#include <atomic>
#include <cstdint>
#include "core/contract.hpp"
#include "strategy/StrategyContext.hpp"

namespace chimera {

// ---------------------------------------------------------------------------
// StrategyRunner: polls the book, feeds an IEngine via onTick, and submits
// any resulting OrderIntents through ExecutionRouter via StrategyContext.
//
// One runner per engine. Each runs in its own pinned thread (CORE1).
// The engine itself is stateless w.r.t. the runtime — it sees MarketTicks
// and produces OrderIntents. The runner owns the poll loop and the submission.
//
// Client ID format: "<engine_id>_<seq>" where seq is a monotonic counter
// per runner. This guarantees uniqueness across all engines without locks.
// ---------------------------------------------------------------------------
class StrategyRunner {
public:
    StrategyRunner(IEngine* engine, StrategyContext& ctx);

    // Blocking poll loop — exits when running becomes false.
    // Call from a ThreadModel on CORE1.
    void run(std::atomic<bool>& running);

private:
    IEngine*          engine_;
    StrategyContext&  ctx_;

    // ---------------------------------------------------------------------------
    // Global atomic sequence counter shared across all runners.
    // Even though engine_->id() already namespaces the prefix, a shared counter
    // eliminates any theoretical collision if two runners call make_client_id()
    // at the exact same nanosecond (e.g. on wake from a simultaneous sleep).
    // Static: one counter for the entire process lifetime. No locks needed.
    // ---------------------------------------------------------------------------
    static std::atomic<uint64_t> seq_;

    // Symbols this runner polls. All engines receive ticks for all symbols —
    // the engine itself filters (e.g. BTCascade returns early if not BTCUSDT).
    static constexpr const char* SYMBOLS[] = {"BTCUSDT", "ETHUSDT", "SOLUSDT"};
    static constexpr int         N_SYMBOLS = 3;

    // ---------------------------------------------------------------------------
    // Per-symbol submission cooldown. After a successful submit on symbol[i],
    // no further submits are allowed for that symbol until COOLDOWN_NS has elapsed.
    // This prevents the runner from spamming the router when an engine fires on
    // the same signal across consecutive polls (e.g. BTC mid ticks >$2 repeatedly
    // at 10k polls/sec). ExecutionThrottle would catch the excess anyway, but
    // gating here avoids constructing, risk-checking, and routing dead orders.
    //
    // 50ms cooldown per symbol. 3 engines × 3 symbols = at most 9 live orders
    // per 50ms window = 180 orders/sec theoretical max. Well within throttle
    // limits (20 global / 5 per-symbol per second).
    // ---------------------------------------------------------------------------
    static constexpr uint64_t COOLDOWN_NS = 50'000'000;  // 50ms
    uint64_t last_submit_ns_[N_SYMBOLS] = {0, 0, 0};

    std::string make_client_id();
};

} // namespace chimera
