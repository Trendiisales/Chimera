#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <vector>
#include <memory>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>

namespace chimera {

class Context;

// ---------------------------------------------------------------------------
// Persistent WebSocket execution channel to Binance WS Trading API.
//
// Replaces REST for order placement and cancel on the hot path.
// REST is retained ONLY for:
//   - ExchangeTruthLoop (periodic position verification)
//   - Cold start reconciliation
//   - Cancel federation emergency sweep (fallback — REST is fire-and-forget
//     for sweep, doesn't need ACK latency)
//
// Architecture:
//   - Dedicated thread owns the Beast SSL WebSocket stream + io_context.
//   - send_order() / cancel_order() push JSON frames into a mutex-protected
//     outbound queue. The WS thread drains it on each loop iteration.
//   - Responses are parsed for latency measurement and rejection handling.
//     ACK/fill lifecycle events are left to the user stream (BinanceWSUser)
//     to avoid double-acking the OSM. WS exec handles ONLY:
//       • Latency measurement (pending_ timestamps → update_latency_us)
//       • Rejection (status != 200 → osm.on_reject)
//   - Reconnect on disconnect with exponential backoff (1s → 30s).
//
// Latency measurement:
//   t_send = userspace timestamp when frame is queued (pending_ insert).
//   t_ack  = userspace timestamp when response is received.
//   latency_us = t_ack - t_send. Fed into ctx_.latency.update_latency_us().
//   This is the end-to-end order ACK latency that matters for queue position.
//
// Threading:
//   - send_order() / cancel_order(): called from CORE1 (ExecutionRouter).
//     Only touches outbound_ queue (mutex) and pending_ map (mutex).
//   - WS thread: owns Beast stream. Drains outbound_, reads responses.
//     Calls ctx_.latency.update_latency_us() and ctx_.osm.on_reject() —
//     both are thread-safe (atomic / mutex internally).
//   - connected_: relaxed atomic, written by WS thread, read by CORE1.
// ---------------------------------------------------------------------------
class BinanceWSExecution {
public:
    explicit BinanceWSExecution(Context& ctx);
    ~BinanceWSExecution();

    void start();
    void stop();

    // Hot path — called from CORE1. Queues frame for WS thread to send.
    // Non-blocking: pushes to outbound queue, returns immediately.
    void send_order(const std::string& symbol, const std::string& side,
                    double qty, double price, const std::string& client_id);

    void cancel_order(const std::string& symbol, const std::string& client_id);

    bool connected() const { return connected_.load(std::memory_order_relaxed); }

private:
    // ---------------------------------------------------------------------------
    // WS thread internals — called only from the WS thread.
    // ---------------------------------------------------------------------------
    void ws_thread_fn();
    void handle_response(const std::string& msg);
    std::string sign(const std::string& payload);
    uint64_t now_us() const;

    Context& ctx_;

    std::string api_key_;
    std::string api_secret_;
    bool futures_;

    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};

    // ---------------------------------------------------------------------------
    // Outbound frame queue — push from CORE1, drain from WS thread.
    // ---------------------------------------------------------------------------
    struct OutboundFrame {
        std::string json;
    };
    std::mutex outbound_mtx_;
    std::vector<OutboundFrame> outbound_;

    // ---------------------------------------------------------------------------
    // Pending orders — client_id (or "cancel_<id>") → send timestamp (us).
    // Insert: CORE1 (send_order). Erase: WS thread (handle_response).
    // Mutex protects cross-thread access.
    // ---------------------------------------------------------------------------
    std::mutex pending_mtx_;
    std::unordered_map<std::string, uint64_t> pending_;

    std::thread ws_thread_;
};

} // namespace chimera
