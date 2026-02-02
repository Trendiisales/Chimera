#include "execution/ExecutionRouter.hpp"
#include "execution/CancelFederation.hpp"
#include "execution/QueueDecayGovernor.hpp"
#include "forensics/EdgeAttribution.hpp"
#include "control/DeskArbiter.hpp"
#include "exchange/binance/BinanceRestClient.hpp"
#include "exchange/binance/BinanceWSExecution.hpp"
#include "forensics/EventTypes.hpp"
#include <chrono>
#include <iostream>
#include <cstring>
#include <vector>
#include <nlohmann/json.hpp>

using namespace chimera;
using json = nlohmann::json;

static uint64_t now_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(
        steady_clock::now().time_since_epoch()).count();
}

ExecutionRouter::ExecutionRouter(Context& ctx)
    : ctx_(ctx),
      throttle_(20, 5) {}

bool ExecutionRouter::submit_order(const std::string& client_id,
                                    const std::string& symbol,
                                    double price, double qty,
                                    const std::string& engine_id) {
    // ---------------------------------------------------------------------------
    // DRIFT KILL GATE — system is dead. Nothing enters the pipeline.
    // Checked before everything, both shadow and live.
    // ---------------------------------------------------------------------------
    if (ctx_.risk.killed()) {
        return false;
    }

    // ---------------------------------------------------------------------------
    // FREEZE GATE: Cancel Federation active = no new orders. Period.
    // This is checked before everything — a pending sweep means the system
    // is in an unsafe state and nothing should enter the pipeline.
    // ---------------------------------------------------------------------------
    if (ctx_.cancel_fed.active()) {
        return false;
    }

    // ---------------------------------------------------------------------------
    // PnL strategy gate — checked before risk or throttle.
    // A killed strategy should not consume risk budget or throttle slots.
    // ---------------------------------------------------------------------------
    if (!ctx_.pnl.allow_strategy(engine_id)) {
        return false;
    }

    // ---------------------------------------------------------------------------
    // Desk Arbiter gate — checked after PnL (PnL is per-engine, desk is per-group).
    // If null, desk governance is not configured — allow.
    // ---------------------------------------------------------------------------
    if (ctx_.desk && !ctx_.desk->allow_submit(engine_id)) {
        std::cout << "[DESK] BLOCK " << engine_id << " " << symbol << "\n";
        return false;
    }

    // ---------------------------------------------------------------------------
    // ATOMIC POSITION GATE — prevents race condition at position caps.
    // Multiple engines can poll simultaneously and all see pos=0.04 < 0.05.
    // Without atomic check, all submit → position becomes 0.07 (violation).
    // Lock held for: read position → check cap → decision.
    // Actual position update happens on fill, but this reserves the capacity.
    // ---------------------------------------------------------------------------
    {
        std::lock_guard<std::mutex> lock(position_gate_mtx_);
        
        double current_pos = ctx_.risk.get_position(symbol);
        double next_pos = current_pos + qty;
        
        static constexpr double MAX_POSITION = 0.05;
        
        if (std::fabs(next_pos) > MAX_POSITION) {
            std::cout << "[POSITION_GATE] BLOCK " << engine_id << " " << symbol 
                      << " would exceed cap: current=" << current_pos 
                      << " + qty=" << qty << " = " << next_pos << "\n";
            ctx_.telemetry.increment_risk_block();
            return false;
        }
    }

    // ---------------------------------------------------------------------------
    // Live-mode gates: risk governor + execution throttle.
    // ---------------------------------------------------------------------------
    if (ctx_.arm.live_enabled()) {
        if (!ctx_.risk.pre_check(symbol, price, qty)) {
            ctx_.telemetry.increment_risk_block();
            return false;
        }

        if (!throttle_.allow_global() || !throttle_.allow_symbol(symbol)) {
            ctx_.telemetry.increment_throttle_block();
            return false;
        }
    }

    // ---------------------------------------------------------------------------
    // Live-mode queue competitiveness gate.
    // In live, reject orders with very low fill probability to avoid wasting
    // throttle slots and risk budget on hopeless orders.
    // In shadow, accept everything — TTL cancel cleans up orders that never fill.
    // ---------------------------------------------------------------------------
    if (ctx_.arm.live_enabled()) {
        auto est = ctx_.queue.estimate(symbol, price, qty, qty > 0);
        if (est.expected_fill_prob < 0.05) {
            std::cout << "[QUEUE] Low fill prob (" << est.expected_fill_prob << ") " << symbol << "\n";
            return false;
        }
    }

    // ---------------------------------------------------------------------------
    // ENGINE+SYMBOL DEDUP — at most one pending order per engine per symbol.
    //
    // If this engine already has a pending order on this symbol:
    //   Same price  → duplicate signal, drop silently. Engine will fire again
    //                 after cooldown but nothing enters the pipeline.
    //   Different price → cancel-replace. Old order is dead (price has moved),
    //                 cancel it and accept the new one at the updated price.
    //
    // This is the actual cancel-replace the coalescer is named for. Without it,
    // every engine tick at 50ms cooldown creates a new pending order that never
    // fills (book hasn't crossed), piling up indefinitely.
    // ---------------------------------------------------------------------------
    {
        std::string existing_cid;
        CoalesceOrder existing_ord;
        if (coalescer_.find_by_engine_symbol(engine_id, symbol, existing_cid, existing_ord)) {
            if (existing_ord.price == price) {
                // Same price — duplicate. Silent drop.
                return false;
            }
            // Different price — cancel old, proceed with new.
            ctx_.osm.on_cancel_by_client_id(existing_cid);
            coalescer_.clear(existing_cid);
            if (ctx_.queue_decay) {
                ctx_.queue_decay->on_order_done(existing_cid);
            }
            if (ctx_.edge) {
                ctx_.edge->on_cancel(existing_cid);
            }
        }
    }

    // B1 FIX: CoalesceOrder, not PendingOrder
    CoalesceOrder ord{symbol, engine_id, price, qty};
    coalescer_.submit(client_id, ord);

    // FIX 4.3: OSM now lives in ctx_.osm
    OrderRecord rec;
    rec.client_id = client_id;
    rec.symbol    = symbol;
    rec.price     = price;
    rec.qty       = qty;
    ctx_.osm.on_new(rec);

    // ---------------------------------------------------------------------------
    // Queue Decay: start tracking this order's age. Live only (no-op in shadow).
    // ---------------------------------------------------------------------------
    if (ctx_.queue_decay) {
        ctx_.queue_decay->on_order_submitted(client_id, symbol, price, qty > 0);
    }

    // ---------------------------------------------------------------------------
    // Edge Attribution: record predicted edge at submit time.
    // predicted_edge_bps = distance from mid in bps, signed by side.
    //   BUY below mid  → positive (providing liquidity, expect to capture spread)
    //   BUY above mid  → negative (taking, paying spread)
    //   SELL above mid → positive (providing)
    //   SELL below mid → negative (taking)
    // This is a direct measurement from the live book — no simulation.
    // ---------------------------------------------------------------------------
    if (ctx_.edge) {
        TopOfBook tb = ctx_.queue.top(symbol);
        double predicted_edge_bps = 0.0;
        if (tb.valid && price > 0.0) {
            double mid  = (tb.bid + tb.ask) * 0.5;
            double sign = (qty > 0) ? 1.0 : -1.0;  // BUY=+1, SELL=-1
            predicted_edge_bps = sign * (mid - price) / price * 10000.0;
        }
        ctx_.edge->on_submit(client_id, engine_id, predicted_edge_bps, 0.0);
    }

    // Forensic: record decision event
    struct DecisionEvent {
        char   symbol[16];
        double price;
        double qty;
    } ev{};
    std::strncpy(ev.symbol, symbol.c_str(), sizeof(ev.symbol) - 1);
    ev.price = price;
    ev.qty   = qty;

    uint64_t causal = ctx_.recorder.next_causal_id();
    ctx_.recorder.write(EventType::DECISION, &ev, sizeof(ev), causal);

    return true;
}

void ExecutionRouter::poll() {
    uint64_t ts = now_ns();

    // ---------------------------------------------------------------------------
    // Periodic OSM purge — terminal orders (FILLED/CANCELED/REJECTED) accumulate
    // in orders_ forever. Purge every 10s to prevent unbounded memory growth.
    // ---------------------------------------------------------------------------
    static uint64_t last_purge_ns = 0;
    if (ts - last_purge_ns > 10'000'000'000ULL) {
        ctx_.osm.purge_terminal();
        last_purge_ns = ts;
    }

    // ---------------------------------------------------------------------------
    // CANCEL FEDERATION SWEEP — runs first, before anything else.
    //
    // If cancel_fed is active, we are the designated sweeper (CORE1).
    // Steps:
    //   1. Log the reason
    //   2. Cancel all in-flight orders via REST (live) or clear coalescer (shadow)
    //   3. Fire drift kill — system is dead after this
    //   4. Clear the cancel_fed flag (sweep complete)
    //
    // After drift kill, the system won't recover without operator intervention.
    // This is intentional — cancel federation is a FATAL event.
    // ---------------------------------------------------------------------------
    if (ctx_.cancel_fed.active()) {
        const char* reason = ctx_.cancel_fed.reason();
        std::cerr << "[CANCEL_FED] SWEEP: reason=" << (reason ? reason : "unknown") << "\n";

        // Cancel all in-flight orders
        auto keys = coalescer_.pending_keys();
        int cancel_count = 0;

        for (const auto& client_id : keys) {
            CoalesceOrder ord;
            if (!coalescer_.get(client_id, ord)) continue;

            if (ctx_.arm.live_enabled() && submitted_.count(client_id) && rest_client_) {
                // Cancel federation sweep: REST fire-and-forget. System is dying —
                // latency is irrelevant. REST is more reliable for sweep because
                // it's synchronous and doesn't depend on WS connectivity.
                rest_client_->cancel_order_by_client_id(ord.symbol, client_id);
                cancel_count++;
            }

            // Clear local state regardless — we're going to drift kill anyway
            coalescer_.clear(client_id);
            submitted_.erase(client_id);
        }

        std::cerr << "[CANCEL_FED] SENT " << cancel_count << " cancels to exchange. "
                  << "Firing drift kill.\n";

        ctx_.cancel_fed.clear();
        ctx_.risk.drift().trigger(std::string("Cancel Federation: ") + (reason ? reason : "unknown"));
        return;  // System is dead. Don't process anything else.
    }

    // ---------------------------------------------------------------------------
    // Portfolio kill check — runs in BOTH modes (shadow PnL tracking is real).
    // ---------------------------------------------------------------------------
    if (ctx_.pnl.portfolio_killed()) {
        if (!ctx_.risk.killed()) {
            std::cerr << "[ROUTER] Portfolio killed by PnLGovernor — triggering drift kill\n";
            ctx_.risk.drift().trigger("PnL governor: portfolio DD breached");
        }
        return;
    }

    // ---------------------------------------------------------------------------
    // Queue Decay poll — check all tracked orders for TTL/urgency breach.
    // May fire cancel_fed (which we'll sweep on the next poll tick).
    // ---------------------------------------------------------------------------
    if (ctx_.queue_decay) {
        ctx_.queue_decay->poll();
    }

    if (!ctx_.arm.live_enabled()) {
        // ---------------------------------------------------------------------------
        // SHADOW EXECUTION PATH — no simulation.
        //
        // We have live book data from BinanceWSMarket. Shadow mode means real money
        // is not trading, but everything else is real. Fill decisions are made
        // purely from the live order book:
        //
        //   BUY  fills when ask <= order_price  (market is selling at/below our bid)
        //   SELL fills when bid >= order_price  (market is buying at/above our ask)
        //
        // Cancel: TTL only. If the order hasn't filled within max_wait, cancel it.
        // No queue position estimation, no fill probability models.
        // ---------------------------------------------------------------------------
        auto keys = coalescer_.pending_keys();

        for (const auto& client_id : keys) {
            CoalesceOrder ord;
            if (!coalescer_.get(client_id, ord)) continue;

            OrderRecord rec;
            try {
                rec = ctx_.osm.get(client_id);
            } catch (...) {
                coalescer_.clear(client_id);
                continue;
            }

            bool is_buy = ord.qty > 0;
            TopOfBook tb = ctx_.queue.top(ord.symbol);

            // No book data yet — order sits pending until feed arrives.
            if (!tb.valid) continue;

            // ---------------------------------------------------------------------------
            // CANCEL: TTL only. Pass fill_prob=1.0 to disable the probability-based
            // cancel check inside CancelPolicy — only the max_wait TTL fires.
            // ---------------------------------------------------------------------------
            if (ctx_.cancel_policy.should_cancel(ts, rec.last_update_ns, 1.0)) {
                coalescer_.clear(client_id);
                std::string cancel_id = rec.exchange_id.empty() ? client_id : rec.exchange_id;
                ctx_.osm.on_cancel(cancel_id);

                if (ctx_.queue_decay) {
                    ctx_.queue_decay->on_order_done(client_id);
                }

                // Edge Attribution: clean up pending_ entry for canceled order.
                if (ctx_.edge) {
                    ctx_.edge->on_cancel(client_id);
                }

                struct CancelEvent {
                    char   symbol[16];
                    char   client_id[32];
                    double price;
                    double qty;
                } ev{};
                std::strncpy(ev.symbol, ord.symbol.c_str(), sizeof(ev.symbol) - 1);
                std::strncpy(ev.client_id, client_id.c_str(), sizeof(ev.client_id) - 1);
                ev.price = ord.price;
                ev.qty   = ord.qty;

                uint64_t causal = ctx_.recorder.next_causal_id();
                ctx_.recorder.write(EventType::ROUTE, &ev, sizeof(ev), causal);
                continue;
            }

            // ---------------------------------------------------------------------------
            // FILL: live book cross check. Did the market cross our price?
            // ---------------------------------------------------------------------------
            bool crossed = is_buy ? (tb.ask <= ord.price) : (tb.bid >= ord.price);

            if (crossed) {
                // Log ROUTE event
                struct RouteEvent {
                    char   symbol[16];
                    char   client_id[32];
                    double price;
                    double qty;
                } rev{};
                std::strncpy(rev.symbol, ord.symbol.c_str(), sizeof(rev.symbol) - 1);
                std::strncpy(rev.client_id, client_id.c_str(), sizeof(rev.client_id) - 1);
                rev.price = ord.price;
                rev.qty   = ord.qty;

                uint64_t route_causal = ctx_.recorder.next_causal_id();
                ctx_.recorder.write(EventType::ROUTE, &rev, sizeof(rev), route_causal);

                // Fill at order price (standard limit order shadow assumption)
                ctx_.osm.on_ack(client_id, client_id);
                ctx_.osm.on_fill(client_id, std::abs(ord.qty));

                // Log FILL event
                struct FillEvent {
                    char   symbol[16];
                    double qty;
                    double price;
                } fev{};
                std::strncpy(fev.symbol, ord.symbol.c_str(), sizeof(fev.symbol) - 1);
                fev.qty   = std::abs(ord.qty);
                fev.price = ord.price;

                uint64_t fill_causal = ctx_.recorder.next_causal_id();
                ctx_.recorder.write(EventType::FILL, &fev, sizeof(fev), fill_causal);

                // Update risk position
                ctx_.risk.on_execution_ack(ord.symbol, ord.qty);

                // ---------------------------------------------------------------------------
                // PnL: entry quality vs current mid.
                // pnl_delta = (mid - fill_price) * signed_qty
                // Positive = we bought below mid or sold above mid (good fill).
                // ---------------------------------------------------------------------------
                {
                    double mid = (tb.bid + tb.ask) * 0.5;
                    double pnl_delta = (mid - ord.price) * ord.qty;
                    ctx_.pnl.update_fill(ord.engine_id, pnl_delta);

                    double notional = ord.price * std::abs(ord.qty);

                    // Edge Attribution: realized PnL at fill. Latency = 0 (no network in shadow).
                    if (ctx_.edge && notional > 0.0) {
                        double realized_bps = (pnl_delta / notional) * 10000.0;
                        ctx_.edge->on_fill(client_id, realized_bps, 0.0);
                    }

                    // Desk Arbiter: feed fill PnL
                    if (ctx_.desk && notional > 0.0) {
                        double pnl_bps = (pnl_delta / notional) * 10000.0;
                        ctx_.desk->on_fill(ord.engine_id, pnl_bps);
                    }
                }

                // Queue decay: order is done
                if (ctx_.queue_decay) {
                    ctx_.queue_decay->on_order_done(client_id);
                }

                // Telemetry
                {
                    ctx_.telemetry.increment_fills();
                    auto positions = ctx_.risk.dump_positions();
                    auto it = positions.find(ord.symbol);
                    double pos_qty  = (it != positions.end()) ? it->second : 0.0;
                    double notional = std::fabs(pos_qty * ord.price);
                    ctx_.telemetry.update_symbol(ord.symbol, pos_qty, notional);
                }

                coalescer_.clear(client_id);
            }
        }
        return;
    }

    // --- LIVE PATH ---

    // 1. Reconciliation trigger
    if (ctx_.needs_reconcile.load()) {
        ctx_.needs_reconcile.store(false);
        live_reconcile();
        if (ctx_.risk.killed()) return;
    }

    // 2. Circuit breaker
    if (rest_failures_ >= CIRCUIT_BREAK_THRESHOLD) return;

    // 3. WS connectivity gate
    if (!ctx_.ws_user_alive.load()) return;

    // 3b. Latency governor cancel-all
    if (ctx_.latency.should_cancel_all()) {
        std::cerr << "[ROUTER] LATENCY CANCEL-ALL — canceling all in-flight orders\n";
        for (const auto& cid : coalescer_.pending_keys()) {
            CoalesceOrder ord;
            if (!coalescer_.get(cid, ord)) continue;
            if (submitted_.count(cid) && ws_exec_) {
                ws_exec_->cancel_order(ord.symbol, cid);
            }
        }
    }

    // 4. Drain coalescer
    auto keys = coalescer_.pending_keys();

    for (const auto& client_id : keys) {
        CoalesceOrder ord;
        if (!coalescer_.get(client_id, ord)) continue;

        OrderRecord rec;
        try {
            rec = ctx_.osm.get(client_id);
        } catch (...) {
            coalescer_.clear(client_id);
            submitted_.erase(client_id);
            if (ctx_.queue_decay) ctx_.queue_decay->on_order_done(client_id);
            continue;
        }

        switch (rec.status) {
            case OrderStatus::NEW:
                if (submitted_.count(client_id) == 0) {
                    live_submit(client_id, ord);
                }
                break;

            case OrderStatus::ACKED:
            case OrderStatus::PARTIALLY_FILLED: {
                bool is_buy = ord.qty > 0;
                auto est = ctx_.queue.estimate(ord.symbol, ord.price,
                                               std::abs(ord.qty), is_buy);
                if (ctx_.cancel_policy.should_cancel(ts, rec.last_update_ns,
                                                      est.expected_fill_prob)) {
                    live_cancel(client_id, rec);
                }
                break;
            }

            case OrderStatus::FILLED:
            case OrderStatus::CANCELED:
            case OrderStatus::REJECTED:
                coalescer_.clear(client_id);
                submitted_.erase(client_id);
                if (ctx_.queue_decay) ctx_.queue_decay->on_order_done(client_id);
                break;
        }
    }
}

void ExecutionRouter::live_submit(const std::string& client_id,
                                   const CoalesceOrder& ord) {
    // ---------------------------------------------------------------------------
    // WS connectivity gate — if WS exec is down, we can't submit.
    // Circuit breaker fires on consecutive poll cycles with no connectivity.
    // ---------------------------------------------------------------------------
    if (!ws_exec_ || !ws_exec_->connected()) {
        rest_failures_++;
        std::cout << "[LIVE] WS exec not connected (" << rest_failures_ << "/"
                  << CIRCUIT_BREAK_THRESHOLD << ")\n";
        if (rest_failures_ >= CIRCUIT_BREAK_THRESHOLD) {
            std::cout << "[LIVE] CIRCUIT BREAKER TRIPPED — WS exec disconnected\n";
            ctx_.risk.drift().trigger("WS exec circuit breaker: " +
                std::to_string(rest_failures_) + " consecutive polls disconnected");
        }
        return;
    }

    bool is_buy  = ord.qty > 0;
    std::string side = is_buy ? "BUY" : "SELL";

    // Non-blocking: queues frame into ws_exec_ outbound queue.
    // WS thread drains and sends on next loop iteration.
    ws_exec_->send_order(ord.symbol, side, std::abs(ord.qty), ord.price, client_id);

    // Mark as submitted immediately — the order is in the WS queue.
    // If WS thread fails to send (disconnect), circuit breaker catches it
    // on next poll via connected() check.
    submitted_.insert(client_id);
    rest_failures_ = 0;

    // Latency measurement: record submit timestamp.
    // on_ack() from user stream completes the RTT measurement.
    ctx_.latency.record_submit_ns(client_id);

    // Forensic: ROUTE event
    struct RouteEvent {
        char   symbol[16];
        char   client_id[32];
        double price;
        double qty;
    } ev{};
    std::strncpy(ev.symbol, ord.symbol.c_str(), sizeof(ev.symbol) - 1);
    std::strncpy(ev.client_id, client_id.c_str(), sizeof(ev.client_id) - 1);
    ev.price = ord.price;
    ev.qty   = ord.qty;

    uint64_t causal = ctx_.recorder.next_causal_id();
    ctx_.recorder.write(EventType::ROUTE, &ev, sizeof(ev), causal);

    std::cout << "[LIVE] Submitted: " << ord.symbol << " " << side
              << " " << std::abs(ord.qty) << " @ " << ord.price
              << " id=" << client_id << "\n";
}

void ExecutionRouter::live_cancel(const std::string& client_id,
                                   const OrderRecord& rec) {
    if (!ws_exec_ || !ws_exec_->connected()) {
        // WS down — can't cancel. Don't increment rest_failures_ here;
        // live_submit already handles the circuit breaker on connectivity.
        std::cout << "[LIVE] Cancel skipped (WS exec disconnected): " << client_id << "\n";
        return;
    }

    // Non-blocking: queues cancel frame.
    ws_exec_->cancel_order(rec.symbol, client_id);

    struct CancelEvent {
        char   symbol[16];
        char   client_id[32];
        double price;
        double qty;
    } ev{};
    std::strncpy(ev.symbol, rec.symbol.c_str(), sizeof(ev.symbol) - 1);
    std::strncpy(ev.client_id, client_id.c_str(), sizeof(ev.client_id) - 1);
    ev.price = rec.price;
    ev.qty   = rec.qty;

    uint64_t causal = ctx_.recorder.next_causal_id();
    ctx_.recorder.write(EventType::CANCEL, &ev, sizeof(ev), causal);

    std::cout << "[LIVE] Cancel submitted: " << rec.symbol
              << " id=" << client_id << "\n";
}

void ExecutionRouter::live_reconcile() {
    if (!rest_client_) return;

    std::cout << "[LIVE] Reconciliation triggered (WS reconnect). "
              << submitted_.size() << " orders in flight.\n";

    std::string open_orders_json;
    try {
        open_orders_json = rest_client_->get_open_orders();
        rest_failures_ = 0;
    } catch (const std::exception& e) {
        std::cout << "[LIVE] Reconcile REST failed: " << e.what() << "\n";
        rest_failures_++;
        ctx_.risk.drift().trigger("Reconciliation REST failure: " +
            std::string(e.what()));
        return;
    }

    std::unordered_set<std::string> exchange_open;
    try {
        json orders = json::parse(open_orders_json);
        if (!orders.is_array()) {
            std::cout << "[LIVE] Reconcile: unexpected response format\n";
            ctx_.risk.drift().trigger("Reconciliation: non-array response");
            return;
        }
        for (const auto& ord : orders) {
            if (ord.contains("origClientOrderId")) {
                exchange_open.insert(ord["origClientOrderId"].get<std::string>());
            }
        }
    } catch (const std::exception& e) {
        std::cout << "[LIVE] Reconcile parse failed: " << e.what() << "\n";
        ctx_.risk.drift().trigger("Reconciliation parse failure");
        return;
    }

    // Cross-reference
    std::vector<std::string> to_remove;
    for (const auto& cid : submitted_) {
        if (exchange_open.count(cid) == 0) {
            std::cout << "[LIVE] Reconcile: " << cid
                      << " not on exchange → REJECTED\n";
            ctx_.osm.on_reject(cid);
            coalescer_.clear(cid);
            if (ctx_.queue_decay) ctx_.queue_decay->on_order_done(cid);
            to_remove.push_back(cid);
        }
    }

    for (const auto& cid : to_remove)
        submitted_.erase(cid);

    for (const auto& exch_cid : exchange_open) {
        if (submitted_.count(exch_cid) == 0) {
            std::cout << "[LIVE] PHANTOM ORDER: " << exch_cid << " — HARD KILL\n";
            ctx_.risk.drift().trigger("Phantom order detected: " + exch_cid);
            return;
        }
    }

    std::cout << "[LIVE] Reconciliation complete. "
              << submitted_.size() << " orders confirmed in flight.\n";
}
