#include "execution/QueueDecayGovernor.hpp"
#include "runtime/Context.hpp"
#include "execution/CancelFederation.hpp"
#include <chrono>
#include <iostream>

using namespace chimera;

static inline uint64_t now_ns() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(
            steady_clock::now().time_since_epoch()
        ).count()
    );
}

QueueDecayGovernor::QueueDecayGovernor(Context& ctx)
    : ctx_(ctx) {}

void QueueDecayGovernor::on_order_submitted(
    const std::string& client_id,
    const std::string& symbol,
    double price, bool is_buy)
{
    if (!ctx_.arm.live_enabled()) return;

    live_[client_id] = TrackedOrder{now_ns(), symbol, price, is_buy};
}

void QueueDecayGovernor::on_order_done(const std::string& client_id) {
    live_.erase(client_id);
}

void QueueDecayGovernor::poll() {
    if (!ctx_.arm.live_enabled()) return;
    if (live_.empty()) return;

    uint64_t now = now_ns();
    uint64_t latency_us = ctx_.latency.last_latency_us();

    for (auto it = live_.begin(); it != live_.end(); ) {
        const auto& client_id = it->first;
        const auto& tr        = it->second;

        // Order no longer open in OSM (filled/canceled by user stream) —
        // clean up tracking. This catches orders that resolved between
        // our submit and this poll.
        if (!ctx_.osm.is_open(client_id)) {
            it = live_.erase(it);
            continue;
        }

        uint64_t age_ns = now - tr.submit_ns;

        // ---------------------------------------------------------------------------
        // HARD TTL: any live HFT order surviving 5s is a system failure.
        // Normal fills happen in <1s. If an order is still alive at 5s,
        // either the exchange is broken, WS is dead, or we're stuck.
        // Cancel Federation. Hard kill.
        // ---------------------------------------------------------------------------
        if (age_ns > hard_ttl_ns_) {
            std::cerr << "[DECAY] HARD TTL: " << client_id
                      << " age=" << (age_ns / 1'000'000) << "ms"
                      << " — CANCEL FEDERATION\n";
            ctx_.cancel_fed.trigger("QUEUE_HARD_TTL");
            return;  // Cancel fed fired. Don't process more orders.
        }

        // ---------------------------------------------------------------------------
        // SOFT TTL + LATENCY URGENCY: after soft_ttl, re-estimate queue position.
        // Urgency = fill_prob_inverse * latency_factor.
        //   fill_prob_inverse: 1/(fill_prob+eps). Low fill prob = high urgency.
        //   latency_factor: 1 + latency_us * latency_k. High latency amplifies.
        //
        // If urgency > threshold → Cancel Federation.
        // This catches: "order has low fill prob AND we're on a slow link."
        // Both conditions together = adverse selection is certain.
        // ---------------------------------------------------------------------------
        if (age_ns > soft_ttl_ns_) {
            bool is_buy = tr.is_buy;
            auto est = ctx_.queue.estimate(tr.symbol, tr.price,
                                           0.0005, is_buy);  // use min lot size

            double fill_prob_inv = 1.0 / (est.expected_fill_prob + 1e-6);
            double latency_factor = 1.0 + static_cast<double>(latency_us) * latency_k_;
            double urgency = fill_prob_inv * latency_factor;

            if (urgency > urgency_threshold_) {
                std::cerr << "[DECAY] URGENCY BREACH: " << client_id
                          << " age=" << (age_ns / 1'000'000) << "ms"
                          << " fill_prob=" << est.expected_fill_prob
                          << " latency=" << latency_us << "us"
                          << " urgency=" << urgency
                          << " — CANCEL FEDERATION\n";
                ctx_.cancel_fed.trigger("QUEUE_URGENCY");
                return;
            }
        }

        ++it;
    }
}
