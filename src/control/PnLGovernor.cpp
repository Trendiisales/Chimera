#include "control/PnLGovernor.hpp"
#include <iostream>
#include <cmath>

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

void PnLGovernor::update_fill(const std::string& strategy, double pnl_delta,
                              double edge_bps, double fee_bps,
                              double slip_bps, double lat_bps,
                              double notional, bool is_maker) {
    std::lock_guard<std::mutex> lock(mtx_);

    auto& s = stats_[strategy];
    s.realized_pnl += pnl_delta;
    portfolio_pnl_ += pnl_delta;
    
    // Track fills and win/loss
    s.fills++;
    if (pnl_delta > 0) {
        s.wins++;
        s.total_win += pnl_delta;
    } else {
        s.losses++;
        s.total_loss += pnl_delta;
    }
    
    // Update execution quality metrics (exponential moving average)
    double alpha = 0.1;
    s.avg_edge_bps = (1.0 - alpha) * s.avg_edge_bps + alpha * edge_bps;
    s.avg_fee_bps = (1.0 - alpha) * s.avg_fee_bps + alpha * fee_bps;
    s.avg_slip_bps = (1.0 - alpha) * s.avg_slip_bps + alpha * slip_bps;
    s.avg_lat_bps = (1.0 - alpha) * s.avg_lat_bps + alpha * lat_bps;
    
    // Track maker/taker ratio
    if (is_maker) {
        s.maker_fills++;
    } else {
        s.taker_fills++;
    }
    
    s.total_notional += notional;

    update_ev(s, pnl_delta);

    // Per-strategy kill
    if (s.rolling_ev < ev_floor_ && !s.killed) {
        s.killed = true;
        std::cerr << "[PNL] STRATEGY KILLED " << strategy
                  << " rolling_ev=" << s.rolling_ev
                  << " realized=" << s.realized_pnl
                  << " floor=" << ev_floor_ << "\n";
    }

    // Portfolio kill
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
    if (it == stats_.end()) return true;
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
    static constexpr double ALPHA = 0.1;
    stats.rolling_ev = (1.0 - ALPHA) * stats.rolling_ev + ALPHA * delta;
}
