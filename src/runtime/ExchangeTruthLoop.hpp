#pragma once

#include <atomic>
#include <thread>
#include <chrono>
#include <string>

namespace chimera {

class Context;
class BinanceRestClient;

// ---------------------------------------------------------------------------
// Periodic exchange truth enforcement — LIVE MODE ONLY.
//
// Every N seconds, pulls positions + open orders from Binance via REST and
// verifies against local state.
//
// POSITIONS: logged for operator visibility only. Full position diff is NOT
//   performed because live mode does not yet track positions via
//   on_execution_ack on fill events. Diffing against local state would
//   produce false drift kills. Enable the diff when live position tracking
//   is wired end-to-end.
//
// ORDERS: full phantom detection in both directions.
//   Exchange ghost: order on exchange that OSM has never seen.
//     → drift kill immediately. Unknown orders = corrupted state or external
//       interference. Neither is safe to continue trading through.
//   Local ghost: order in OSM (open) that is NOT on exchange.
//     → logged only. This is expected transiently: an order in NEW state
//       that hasn't been submitted yet, or a fill/cancel that the user stream
//       delivered but OSM hasn't fully processed. The truth loop runs at 3s
//       intervals — transient states should have resolved. If they persist
//       across 2+ cycles, THEN it's a problem. For now: log and let the
//       normal cancel policy + reconciliation path handle it.
//
// SHADOW MODE: loop sleeps, does nothing. No REST calls, zero CPU.
//
// THREADING: dedicated thread. All state it reads (OSM, risk) is
//   mutex-protected internally. REST client MUST be a separate instance —
//   CURL easy handles are not thread-safe. Caller constructs a dedicated
//   BinanceRestClient and passes it here.
// ---------------------------------------------------------------------------
class ExchangeTruthLoop {
public:
    ExchangeTruthLoop(Context& ctx, std::chrono::seconds interval);
    ~ExchangeTruthLoop();

    // Wire the REST client. Must be a DEDICATED instance (CURL not thread-safe).
    // Caller owns lifetime — it must outlive this object.
    // nullptr = loop is a no-op (shadow mode, no keys).
    void set_rest_client(BinanceRestClient* client) { rest_client_ = client; }

    void start();
    void stop();

private:
    void run();
    void check_exchange_state();

    Context&             ctx_;
    BinanceRestClient*   rest_client_{nullptr};
    std::chrono::seconds interval_;
    std::atomic<bool>    running_{false};
    std::thread          worker_;
};

} // namespace chimera
