#pragma once
#include "BinanceSymbolRouter.hpp"
#include "SpscRing.hpp"
#include "LowLatency.hpp"

#include <thread>
#include <atomic>
#include <chrono>

namespace binance {

/*
 One shard = one core.
 Non-copyable, non-movable by design.
 Owned via unique_ptr.
*/
class Shard {
    static constexpr size_t QSIZE = 2048;

    std::thread th;
    std::atomic<bool> running{false};
    SpscRing<DepthDelta, QSIZE> queue;

public:
    SymbolRouter router;

    Shard() = default;
    Shard(const Shard&) = delete;
    Shard& operator=(const Shard&) = delete;
    Shard(Shard&&) = delete;
    Shard& operator=(Shard&&) = delete;

    void start(int cpu_id) {
        running.store(true, std::memory_order_relaxed);
        th = std::thread([this, cpu_id]() {
            pin_thread(cpu_id);

            while (running.load(std::memory_order_relaxed)) {
                DepthDelta d;
                while (queue.pop(d)) {
                    auto& ctx = router.get_or_create(d.symbol);

                    auto r = ctx.gate.evaluate(d);
                    if (r == DeltaResult::DROP_OLD)
                        continue;

                    if (r == DeltaResult::GAP) {
                        ctx.hot.gaps_detected.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }

                    ctx.book.apply_delta(d);
                    ctx.hot.deltas_applied.fetch_add(1, std::memory_order_relaxed);
                }

                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        });
    }

    void stop() {
        running.store(false, std::memory_order_relaxed);
        if (th.joinable())
            th.join();
    }

    bool push(const DepthDelta& d) {
        return queue.push(d);
    }
};

}
