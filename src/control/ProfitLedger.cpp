#include "control/ProfitLedger.hpp"
#include "runtime/Context.hpp"
#include <chrono>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>

using namespace chimera;

static uint64_t now_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

ProfitLedger::ProfitLedger(Context& ctx) : ctx_(ctx) {}

void ProfitLedger::set_engine_defaults(const std::string& engine_id,
                                        double min_edge_bps,
                                        double size_mult,
                                        double soft_ttl_fill_prob) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto& m = engines_[engine_id];
    m.min_edge_bps        = min_edge_bps;
    m.size_multiplier     = size_mult;
    m.soft_ttl_fill_prob  = soft_ttl_fill_prob;
}

void ProfitLedger::on_submit(const std::string& engine_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    engines_[engine_id].submits++;
}

void ProfitLedger::on_fill(const std::string& engine_id,
                            const std::string& symbol,
                            bool is_buy,
                            double fill_price,
                            double fill_qty,
                            uint64_t submit_ns,
                            double latency_us,
                            double predicted_edge_bps,
                            double realized_edge_bps,
                            double fee_bps,
                            double slippage_bps,
                            double pnl_usd,
                            double net_bps) {
    std::lock_guard<std::mutex> lock(mtx_);
    uint64_t now = now_ns();

    auto& m = engines_[engine_id];
    m.fills++;
    m.net_pnl_usd += pnl_usd;

    // 100-fill EMA of net_bps
    m.ev_ema_bps = (1.0 - EV_EMA_ALPHA) * m.ev_ema_bps + EV_EMA_ALPHA * net_bps;

    // Latency sample (capped window)
    m.latency_samples.push_back(latency_us);
    if (m.latency_samples.size() > EngineMetrics::LATENCY_WINDOW) {
        m.latency_samples.pop_front();
    }

    // Kill check (only after enough fills to have a meaningful EMA)
    if (m.fills >= 10) {
        check_kill(engine_id, m, now);
    }

    // Auto-tune trigger
    if (now - last_autotune_ns > AUTOTUNE_INTERVAL_NS) {
        auto_tune();
        last_autotune_ns = now;
    }
}

void ProfitLedger::on_cancel(const std::string& engine_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    engines_[engine_id].cancels++;
}

void ProfitLedger::on_price(const std::string& symbol, double mid, uint64_t ts_ns) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto& vs = vol_[symbol];

    if (vs.prev_mid > 0.0 && vs.prev_ts_ns > 0 && ts_ns > vs.prev_ts_ns) {
        double dt_ms = static_cast<double>(ts_ns - vs.prev_ts_ns) / 1'000'000.0;
        if (dt_ms > 0.05 && vs.prev_mid > 0.0) {  // min 50µs between samples
            double change_bps = std::abs((mid - vs.prev_mid) / vs.prev_mid) * 10000.0;
            double bps_per_ms = change_bps / dt_ms;
            // EMA alpha=0.1 — smooths out tick noise
            vs.vol_bps_per_ms = 0.9 * vs.vol_bps_per_ms + 0.1 * bps_per_ms;
        }
    }
    vs.prev_mid   = mid;
    vs.prev_ts_ns = ts_ns;
}

double ProfitLedger::admission_threshold(const std::string& engine_id,
                                          const std::string& symbol,
                                          double latency_us,
                                          double fill_prob,
                                          bool is_buy) {
    std::lock_guard<std::mutex> lock(mtx_);

    // ---------------------------------------------------------------------------
    // Real cost model — every component is measurable.
    //
    //   fee_bps      = 10.0 (Binance spot, fixed)
    //   latency_bps  = price drift during ACK delay. Proportional to vol and time.
    //                  (latency_us / 1000.0) converts to ms for vol_bps_per_ms.
    //   queue_bps    = cost of not filling. If we have 65% chance of filling,
    //                  we pay 35% * half-spread on the orders that don't fill
    //                  (they get picked off or we cancel after adverse move).
    //
    //   real_cost = sum of all three.
    //   threshold = max(real_cost * SAFETY_MULT, engine_min_edge)
    //
    //   SAFETY_MULT=1.5 prevents slow bleed: we only trade when edge is 50%
    //   above costs, not just above them.
    // ---------------------------------------------------------------------------
    double latency_ms = latency_us / 1000.0;

    // Volatility from EMA tracker (defaults to 0.5 bps/ms if no data yet)
    double vol_bps_per_ms = 0.5;
    auto vit = vol_.find(symbol);
    if (vit != vol_.end() && vit->second.vol_bps_per_ms > 0.0) {
        vol_bps_per_ms = vit->second.vol_bps_per_ms;
    }

    // Spread from cache (defaults to 1.0bps if no data)
    double spread_bps = 1.0;
    auto sit = spread_cache_.find(symbol);
    if (sit != spread_cache_.end()) {
        spread_bps = sit->second.spread_bps;
    }

    double fee_bps_cost     = FEE_BPS;
    double latency_bps_cost = latency_ms * vol_bps_per_ms;
    double queue_bps_cost   = (1.0 - fill_prob) * spread_bps * 0.5;

    double real_cost = fee_bps_cost + latency_bps_cost + queue_bps_cost;

    // Engine min_edge (auto-tuned floor)
    double engine_min = 15.0;  // default if engine not registered
    auto eit = engines_.find(engine_id);
    if (eit != engines_.end()) {
        engine_min = eit->second.min_edge_bps;
    }

    return std::max(real_cost * SAFETY_MULT, engine_min);
}

double ProfitLedger::get_min_edge(const std::string& engine_id) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = engines_.find(engine_id);
    return (it != engines_.end()) ? it->second.min_edge_bps : 15.0;
}

double ProfitLedger::get_size_multiplier(const std::string& engine_id) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = engines_.find(engine_id);
    return (it != engines_.end()) ? it->second.size_multiplier : 1.0;
}

double ProfitLedger::get_soft_ttl_fill_prob(const std::string& engine_id) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = engines_.find(engine_id);
    return (it != engines_.end()) ? it->second.soft_ttl_fill_prob : 0.35;
}

void ProfitLedger::check_kill(const std::string& engine_id,
                               EngineMetrics& m,
                               uint64_t now) {
    if (!m.alive) return;

    if (m.ev_ema_bps < EV_KILL_THRESHOLD) {
        if (m.ev_negative_since_ns == 0) {
            m.ev_negative_since_ns = now;
        }
        if (now - m.ev_negative_since_ns > EV_KILL_SUSTAIN_NS) {
            m.alive = false;
            std::cerr << "[PROFIT] ENGINE KILLED " << engine_id
                      << " ev_ema=" << m.ev_ema_bps << "bps"
                      << " sustained " << (now - m.ev_negative_since_ns) / 1'000'000'000ULL << "s"
                      << " net_pnl=$" << m.net_pnl_usd << "\n";
            ctx_.pnl.block_engine(engine_id);
        }
    } else {
        m.ev_negative_since_ns = 0;  // recovered — reset timer
    }
}

void ProfitLedger::auto_tune() {
    // ---------------------------------------------------------------------------
    // Auto-tune runs every 5 min. For each engine with enough data:
    //
    //   EV > +5bps  → loosen: min_edge -= 1, size *= 1.1  (engine is profitable, grow)
    //   EV < 0      → tighten: min_edge += 2, size *= 0.8 (engine is bleeding, shrink)
    //   FillRate < 15% → soften queue: soft_ttl_fill_prob -= 0.05
    //   CancelRate < 30% → tighten queue: soft_ttl_fill_prob += 0.05
    //
    // Floors: min_edge >= 5.0, size_mult in [0.1, 3.0], fill_prob in [0.15, 0.60]
    // Only tunes engines with >= 5 fills (not enough data = don't touch).
    // ---------------------------------------------------------------------------
    for (auto& kv : engines_) {
        auto& m = kv.second;
        if (!m.alive || m.fills < 5) continue;

        // EV-driven tuning
        if (m.ev_ema_bps > 5.0) {
            m.min_edge_bps   = std::max(5.0, m.min_edge_bps - 1.0);
            m.size_multiplier = std::min(3.0, m.size_multiplier * 1.1);
            std::cout << "[AUTOTUNE] " << kv.first << " EV>+5 → min_edge="
                      << m.min_edge_bps << " size_mult=" << m.size_multiplier << "\n";
        } else if (m.ev_ema_bps < 0.0) {
            m.min_edge_bps   = std::min(50.0, m.min_edge_bps + 2.0);
            m.size_multiplier = std::max(0.1, m.size_multiplier * 0.8);
            std::cout << "[AUTOTUNE] " << kv.first << " EV<0 → min_edge="
                      << m.min_edge_bps << " size_mult=" << m.size_multiplier << "\n";
        }

        // Fill/cancel rate tuning
        uint64_t attempts = m.fills + m.cancels;
        if (attempts > 0) {
            double fill_rate   = static_cast<double>(m.fills) / attempts;
            double cancel_rate = static_cast<double>(m.cancels) / attempts;

            if (fill_rate < 0.15) {
                m.soft_ttl_fill_prob = std::max(0.15, m.soft_ttl_fill_prob - 0.05);
                std::cout << "[AUTOTUNE] " << kv.first << " FillRate<15% → fill_prob="
                          << m.soft_ttl_fill_prob << "\n";
            }
            if (cancel_rate < 0.30) {
                m.soft_ttl_fill_prob = std::min(0.60, m.soft_ttl_fill_prob + 0.05);
                std::cout << "[AUTOTUNE] " << kv.first << " CancelRate<30% → fill_prob="
                          << m.soft_ttl_fill_prob << "\n";
            }
        }
    }
}

double ProfitLedger::latency_p95(const EngineMetrics& m) {
    if (m.latency_samples.empty()) return 0.0;
    std::vector<double> sorted(m.latency_samples.begin(), m.latency_samples.end());
    std::sort(sorted.begin(), sorted.end());
    size_t idx = static_cast<size_t>(0.95 * sorted.size());
    if (idx >= sorted.size()) idx = sorted.size() - 1;
    return sorted[idx];
}

std::string ProfitLedger::to_json() const {
    std::lock_guard<std::mutex> lock(mtx_);
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "{\"engines\":{";

    bool first = true;
    for (const auto& kv : engines_) {
        if (!first) ss << ",";
        first = false;

        const auto& m = kv.second;
        uint64_t attempts = m.fills + m.cancels;
        double fill_rate   = (attempts > 0) ? static_cast<double>(m.fills) / attempts : 0.0;
        double cancel_rate = (attempts > 0) ? static_cast<double>(m.cancels) / attempts : 0.0;
        double lat_p95     = latency_p95(m);

        ss << "\"" << kv.first << "\":{"
           << "\"ev_bps\":"         << m.ev_ema_bps
           << ",\"fill_rate\":"     << fill_rate
           << ",\"cancel_rate\":"   << cancel_rate
           << ",\"latency_p95\":"   << lat_p95
           << ",\"net_pnl\":"       << m.net_pnl_usd
           << ",\"fills\":"         << m.fills
           << ",\"cancels\":"       << m.cancels
           << ",\"min_edge_bps\":"  << m.min_edge_bps
           << ",\"size_mult\":"     << m.size_multiplier
           << ",\"state\":"         << (m.alive ? "\"ALIVE\"" : "\"KILLED\"")
           << "}";
    }

    ss << "}}";
    return ss.str();
}
