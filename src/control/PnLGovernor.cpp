#include "control/PnLGovernor.hpp"
#include <iostream>

using namespace chimera;

PnLGovernor::PnLGovernor() {}

void PnLGovernor::set_strategy_floor(double ev_floor) {
    std::lock_guard<std::mutex> lock(mtx_);
    ev_floor_ = ev_floor;
}

void PnLGovernor::set_portfolio_dd(double max_loss) {
    std::lock_guard<std::mutex> lock(mtx_);
    portfolio_dd_ = max_loss;
}

void PnLGovernor::update_fill(const std::string& strategy, double pnl_delta) {
    std::lock_guard<std::mutex> lock(mtx_);

    auto& s = stats_[strategy];
    s.realized_pnl += pnl_delta;
    portfolio_pnl_ += pnl_delta;

    update_ev(s, pnl_delta);

    // Per-strategy kill. One-shot: once killed, stays killed until reset().
    // In-flight orders are NOT canceled — they resolve via normal lifecycle.
    if (s.rolling_ev < ev_floor_ && !s.killed) {
        s.killed = true;
        std::cerr << "[PNL] STRATEGY KILLED " << strategy
                  << " rolling_ev=" << s.rolling_ev
                  << " realized=" << s.realized_pnl
                  << " floor=" << ev_floor_ << "\n";
    }

    // Portfolio kill. One-shot. Caller checks portfolio_killed() and fires
    // drift kill — we don't touch the runtime kill mechanism here.
    if (portfolio_pnl_ < portfolio_dd_ && !portfolio_killed_) {
        portfolio_killed_ = true;
        std::cerr << "[PNL] PORTFOLIO KILL — total_pnl=" << portfolio_pnl_
                  << " limit=" << portfolio_dd_ << "\n";
    }
}

bool PnLGovernor::allow_strategy(const std::string& strategy) const {
    std::lock_guard<std::mutex> lock(mtx_);

    if (portfolio_killed_) return false;

    auto it = stats_.find(strategy);
    if (it == stats_.end()) return true;  // no history = first trade, allowed
    return !it->second.killed;
}

void PnLGovernor::block_engine(const std::string& engine_id) {
    std::lock_guard<std::mutex> lock(mtx_);

    auto& s = stats_[engine_id];
    if (!s.killed) {
        s.killed = true;
        std::cerr << "[PNL] ENGINE FORCE-BLOCKED: " << engine_id
                  << " (by EdgeAttribution)"
                  << " realized=" << s.realized_pnl << "\n";
    }
}

double PnLGovernor::portfolio_pnl() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return portfolio_pnl_;
}

bool PnLGovernor::portfolio_killed() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return portfolio_killed_;
}

std::unordered_map<std::string, PnLGovernor::StrategyStats> PnLGovernor::dump_stats() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return stats_;
}

void PnLGovernor::reset() {
    std::lock_guard<std::mutex> lock(mtx_);
    stats_.clear();
    portfolio_pnl_ = 0.0;
    portfolio_killed_ = false;
    std::cout << "[PNL] Governor reset — all strategies cleared\n";
}

void PnLGovernor::update_ev(StrategyStats& stats, double delta) {
    // Exponential moving average. Alpha=0.1:
    //   - Each fill contributes 10% weight
    //   - After ~23 fills, initial state has decayed to ~9%
    //   - Persistent losers hit the floor predictably
    //   - A strategy that stops losing recovers EV naturally
    static constexpr double ALPHA = 0.1;
    stats.rolling_ev = (1.0 - ALPHA) * stats.rolling_ev + ALPHA * delta;
}
