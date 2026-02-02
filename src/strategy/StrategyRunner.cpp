#include "strategy/StrategyRunner.hpp"
#include <chrono>
#include <thread>
#include <iostream>
#include <vector>

using namespace chimera;

// Static definition — single counter for all runners in this process.
std::atomic<uint64_t> StrategyRunner::seq_{0};

StrategyRunner::StrategyRunner(IEngine* engine, StrategyContext& ctx)
    : engine_(engine), ctx_(ctx) {}

std::string StrategyRunner::make_client_id() {
    return engine_->id() + "_" + std::to_string(seq_.fetch_add(1));
}

// ---------------------------------------------------------------------------
// Poll loop:
//   1. Read top-of-book for each symbol
//   2. Construct MarketTick
//   3. Call engine->onTick()
//   4. For each OrderIntent returned: risk gate → submit via StrategyContext
//
// Sleep 100µs between iterations. This gives ~10k ticks/sec per engine —
// more than sufficient for shadow fill simulation which itself polls at 50µs.
// Engines that only care about one symbol (BTCascade, ETHSniper) return
// immediately from onTick for non-matching symbols, so the overhead of
// polling all three is negligible.
// ---------------------------------------------------------------------------
void StrategyRunner::run(std::atomic<bool>& running) {
    std::vector<OrderIntent> intents;

    bool engine_dead = false;

    while (running.load()) {
        // ---------------------------------------------------------------------------
        // System kill gate — drift kill fired. ALL engines stop. Fatal.
        // No further intents, no further polls. Thread sleeps until shutdown.
        // ---------------------------------------------------------------------------
        if (ctx_.system_killed()) {
            std::cout << "[STRAT] " << engine_->id() << " — system killed, stopping\n";
            while (running.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            break;
        }

        // ---------------------------------------------------------------------------
        // Per-engine kill gate — PnLGovernor or EdgeAttribution killed this engine.
        // This engine stops generating intents. Other engines continue.
        // Log once on transition, then sleep.
        // ---------------------------------------------------------------------------
        if (!engine_dead && ctx_.engine_killed(engine_->id())) {
            engine_dead = true;
            std::cout << "[STRAT] " << engine_->id() << " KILLED — engine stopped\n";
        }
        if (engine_dead) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        for (int i = 0; i < N_SYMBOLS; ++i) {
            double bid, ask, bid_size, ask_size;
            if (!ctx_.top(SYMBOLS[i], bid, ask, bid_size, ask_size))
                continue;   // no data yet for this symbol — skip

            MarketTick tick;
            tick.symbol   = SYMBOLS[i];
            tick.bid      = bid;
            tick.ask      = ask;
            tick.bid_size = bid_size;
            tick.ask_size = ask_size;
            tick.ts_ns    = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::high_resolution_clock::now().time_since_epoch()
                ).count());

            // Inject current position so engines can gate on position size.
            // Single-symbol read — O(1) with mutex, no map copy.
            tick.position = ctx_.get_position(SYMBOLS[i]);

            intents.clear();
            engine_->onTick(tick, intents);

            for (const auto& intent : intents) {
                // Signed qty: positive = buy, negative = sell
                double signed_qty = intent.is_buy ? intent.size : -intent.size;

                // Per-symbol cooldown: suppress repeat submissions from the same
                // signal firing on consecutive polls. Checked before risk gate
                // to avoid wasted pre_check calls on cooldown-blocked intents.
                if (tick.ts_ns - last_submit_ns_[i] < COOLDOWN_NS) continue;

                // Cooldown updated HERE — on any intent that passes the cooldown
                // window, regardless of whether risk or router accepts it.
                // Previously this was inside the "if (accepted)" block, which
                // meant blocked orders never updated the cooldown timestamp.
                // Result: generate → risk_block → generate → risk_block at 10k
                // polls/sec because cooldown never advanced. Now: one intent per
                // symbol per cooldown window, period. The engine will fire again
                // after COOLDOWN_NS even if this one was blocked.
                last_submit_ns_[i] = tick.ts_ns;

                // Risk pre-check — LIVE MODE ONLY.
                // In shadow mode this is skipped entirely. Shadow positions
                // accumulate against risk ceilings with no reset path, so
                // pre_check would block forever once the ceiling is hit.
                // ExecutionRouter's queue probability estimate is the sole
                // shadow gate — same pattern as the router itself.
                if (ctx_.is_live()) {
                    if (!ctx_.allow(intent.symbol, intent.price, signed_qty)) {
                        std::cout << "[STRAT] " << engine_->id()
                                  << " RISK_BLOCK " << intent.symbol << "\n";
                        continue;
                    }
                }

                std::string cid = make_client_id();
                bool accepted = ctx_.submit(cid, intent.symbol,
                                            intent.price, signed_qty,
                                            intent.engine_id);

                if (accepted) {
                    std::cout << "[STRAT] " << engine_->id()
                              << " SUBMIT " << intent.symbol
                              << (intent.is_buy ? " BUY " : " SELL ")
                              << intent.size << " @ " << intent.price
                              << " id=" << cid << "\n";
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    std::cout << "[STRAT] " << engine_->id() << " runner exited (seq=" << seq_ << ")\n";
}
