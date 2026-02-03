#include "runtime/ExchangeTruthLoop.hpp"
#include "runtime/Context.hpp"
#include "exchange/binance/BinanceRestClient.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <unordered_set>
#include <cmath>

using namespace chimera;
using json = nlohmann::json;

ExchangeTruthLoop::ExchangeTruthLoop(Context& ctx, std::chrono::seconds interval)
    : ctx_(ctx), interval_(interval) {}

ExchangeTruthLoop::~ExchangeTruthLoop() {
    stop();
}

void ExchangeTruthLoop::start() {
    if (running_.exchange(true)) return;  // already running
    worker_ = std::thread([this]() { run(); });
}

void ExchangeTruthLoop::stop() {
    if (!running_.exchange(false)) return;  // already stopped
    if (worker_.joinable()) worker_.join();
}

void ExchangeTruthLoop::run() {
    while (running_.load()) {
        std::this_thread::sleep_for(interval_);
        if (!running_.load()) break;

        // LIVE ONLY. Shadow = no-op.
        if (!ctx_.arm.live_enabled()) continue;

        // No REST client = can't verify. Guard defensively — main should have
        // wired one before arming, but don't assume.
        if (!rest_client_) {
            std::cerr << "[TRUTH] No REST client wired — cannot verify exchange state\n";
            continue;
        }

        check_exchange_state();
    }
}

void ExchangeTruthLoop::check_exchange_state() {
    // =========================================================================
    // POSITION SNAPSHOT — INFORMATIONAL ONLY
    //
    // Live position tracking via on_execution_ack on fill events is not yet
    // wired end-to-end. Local positions reflect shadow fills only. Diffing
    // would produce false drift kills. Log any non-zero exchange positions
    // for operator visibility. Full diff enabled when live fill path is complete.
    // =========================================================================
    std::string acct_raw;
    try {
        acct_raw = rest_client_->get_account_snapshot();
    } catch (const std::exception& e) {
        // REST failure in live mode = we're flying blind on positions.
        // This is dangerous enough to kill.
        std::cerr << "[TRUTH] Account fetch failed: " << e.what() << "\n";
        ctx_.risk.drift().trigger("TRUTH LOOP: account fetch failed — " +
            std::string(e.what()));
        return;
    }

    try {
        json acct = json::parse(acct_raw);
        if (acct.contains("positions") && acct["positions"].is_array()) {
            for (const auto& pos : acct["positions"]) {
                if (!pos.contains("positionAmt") || !pos.contains("symbol")) continue;

                std::string amt_str = pos["positionAmt"].is_string()
                    ? pos["positionAmt"].get<std::string>()
                    : std::to_string(pos["positionAmt"].get<double>());

                double amt = std::stod(amt_str);
                if (std::fabs(amt) > 1e-8) {
                    std::cout << "[TRUTH] LIVE POSITION: "
                              << pos["symbol"].get<std::string>()
                              << " qty=" << amt << "\n";
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[TRUTH] Account parse failed: " << e.what() << "\n";
        ctx_.risk.drift().trigger("TRUTH LOOP: account parse failed");
        return;
    }

    // =========================================================================
    // ORDER TRUTH — FULL PHANTOM DETECTION
    //
    // Pull exchange open orders. Cross-reference against OSM.
    //
    // Exchange ghost (phantom): order on exchange that OSM doesn't know.
    //   → HARD KILL via drift trigger. Unknown orders = we're not in control.
    //
    // Local ghost: order in OSM (open) not on exchange.
    //   → LOG ONLY. Expected transiently (NEW not yet submitted, or fill/cancel
    //     event in flight on user stream). Normal cancel policy handles these.
    //     If this persists across multiple cycles, operator will see it in logs.
    // =========================================================================
    std::string orders_raw;
    try {
        orders_raw = rest_client_->get_open_orders();
    } catch (const std::exception& e) {
        // Order fetch failure. Less critical than position failure (orders are
        // ephemeral), but log it. Don't kill on this alone — the user stream
        // reconciliation path will catch order-level drift on reconnect.
        std::cerr << "[TRUTH] Open orders fetch failed: " << e.what() << "\n";
        return;
    }

    // Parse exchange open orders — extract origClientOrderId from each.
    // Binance returns JSON array. Same parsing pattern as BinanceReconciler.
    std::unordered_set<std::string> exchange_open;
    try {
        json orders = json::parse(orders_raw);
        if (!orders.is_array()) {
            std::cerr << "[TRUTH] Unexpected open orders format\n";
            return;
        }
        for (const auto& ord : orders) {
            if (ord.contains("origClientOrderId")) {
                exchange_open.insert(ord["origClientOrderId"].get<std::string>());
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[TRUTH] Orders parse failed: " << e.what() << "\n";
        return;
    }

    // Build set of locally open orders from OSM.
    // dump_orders() acquires its own mutex — safe to call from this thread.
    std::unordered_set<std::string> local_open;
    {
        auto all = ctx_.osm.dump_orders();
        for (const auto& rec : all) {
            if (rec.status == OrderStatus::NEW ||
                rec.status == OrderStatus::ACKED ||
                rec.status == OrderStatus::PARTIALLY_FILLED) {
                local_open.insert(rec.client_id);
            }
        }
    }

    // --- Pass 1: Exchange ghost detection (HARD KILL) ---
    // Any order on exchange that we have no OSM record for at all (not just
    // not-open — no record period). This means something submitted an order
    // we didn't route, or we lost our tracking state.
    for (const auto& exch_cid : exchange_open) {
        try {
            ctx_.osm.get(exch_cid);
            // Record exists (any status) — we know about it.
        } catch (...) {
            // No record at all. Phantom. Hard kill.
            std::cerr << "[TRUTH] EXCHANGE GHOST: " << exch_cid
                      << " — on exchange, not in OSM. KILLING.\n";
            ctx_.risk.drift().trigger("TRUTH LOOP: phantom order on exchange: " + exch_cid);
            return;  // Kill fired. Don't continue.
        }
    }

    // --- Pass 2: Local ghost logging (informational) ---
    // Orders we think are open that aren't on exchange. Transient states are
    // expected. Log for operator visibility.
    for (const auto& local_cid : local_open) {
        if (exchange_open.count(local_cid) == 0) {
            std::cout << "[TRUTH] LOCAL GHOST: " << local_cid
                      << " — in OSM (open) but not on exchange (transient expected)\n";
        }
    }

    // --- Status log ---
    if (exchange_open.empty() && local_open.empty()) {
        std::cout << "[TRUTH] OK — clean (no positions, no orders)\n";
    } else {
        std::cout << "[TRUTH] OK — local_open=" << local_open.size()
                  << " exchange_open=" << exchange_open.size()
                  << " (no phantoms)\n";
    }
}
