#pragma once
#include <string>
#include <atomic>
#include <unordered_set>

#include "runtime/Context.hpp"
#include "execution/ExecutionThrottle.hpp"
#include "execution/CancelReplaceCoalescer.hpp"

namespace chimera {

// Forward declarations — avoids pulling curl/beast headers into this header.
// ExecutionRouter holds raw pointers (non-owning) to clients constructed and
// owned by main().
class BinanceRestClient;   // REST — retained for cancel federation sweep only.
class BinanceWSExecution;  // WS Trading API — hot path submit + cancel.

class ExecutionRouter {
public:
    explicit ExecutionRouter(Context& ctx);

    // Wire the live WS execution client for order submit + cancel (hot path).
    // nullptr = shadow mode (default). Must be called before live arming.
    // Caller owns the BinanceWSExecution lifetime — it must outlive this router.
    void set_ws_exec(BinanceWSExecution* client) { ws_exec_ = client; }

    // Wire REST client for cancel federation sweep ONLY (fire-and-forget fallback).
    // Separate from ws_exec_ — sweep runs when system is dying, latency irrelevant.
    void set_rest_client(BinanceRestClient* client) { rest_client_ = client; }

    bool submit_order(const std::string& client_id,
                      const std::string& symbol,
                      double price, double qty,
                      const std::string& engine_id);

    void poll();

    // Expose queue for external book updates (market feed wires here)
    QueuePositionModel& queue() { return ctx_.queue; }

private:
    Context& ctx_;
    // FIX 4.3: OSM removed from here — now lives in ctx_.osm.
    // This ensures ContextSnapshotter can persist open orders across restarts.
    ExecutionThrottle      throttle_;
    CancelReplaceCoalescer coalescer_;

    // ---------------------------------------------------------------------------
    // Atomic position gate — prevents race condition at position caps.
    // Lock held during position check to ensure no concurrent submissions
    // can violate position limits.
    // ---------------------------------------------------------------------------
    mutable std::mutex position_gate_mtx_;

    // ---------------------------------------------------------------------------
    // Live execution state
    // ---------------------------------------------------------------------------
    // Hot path: WS Trading API for submit + cancel.
    BinanceWSExecution* ws_exec_{nullptr};

    // Cold path: REST client for cancel federation sweep only (fire-and-forget).
    BinanceRestClient* rest_client_{nullptr};

    // Orders submitted to exchange but not yet ACKed (or terminal).
    // Prevents duplicate REST submission on re-poll.
    // Cleared on ACK/FILL/CANCEL/REJECT (terminal OSM states).
    // Survived across WS outages — reconciled on reconnect.
    std::unordered_set<std::string> submitted_;

    // REST circuit breaker. Consecutive failures increment this counter.
    // At threshold: drift kill is triggered and no further submissions occur.
    // Reset to 0 on any successful REST call.
    static constexpr int CIRCUIT_BREAK_THRESHOLD = 3;
    int rest_failures_{0};

    // ---------------------------------------------------------------------------
    // Live path helpers — each runs exclusively on the CORE1 poll thread.
    // No additional locking needed (single consumer of coalescer in live mode).
    // ---------------------------------------------------------------------------

    // Submit a NEW order to Binance via WS Trading API. Adds to submitted_ on queue.
    // ws_exec_ is non-blocking (queues frame). Circuit breaker triggers on
    // WS disconnect, not on per-order failure.
    void live_submit(const std::string& client_id, const CoalesceOrder& ord);

    // Cancel an ACKED order by client ID via WS Trading API. Non-blocking.
    // Does NOT remove from coalescer — waits for CANCELED event on user stream.
    void live_cancel(const std::string& client_id, const OrderRecord& rec);

    // Reconcile in-flight orders against exchange truth after a WS reconnect.
    // Orders we submitted but exchange doesn't know about → REJECTED.
    // Orders on exchange we don't know about → phantom → hard kill.
    void live_reconcile();
};

} // namespace chimera
