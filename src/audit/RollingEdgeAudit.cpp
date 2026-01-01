// =============================================================================
// RollingEdgeAudit.cpp - v4.8.0 - ROLLING EDGE AUDIT IMPLEMENTATION
// =============================================================================
#include "audit/RollingEdgeAudit.hpp"

#include <algorithm>
#include <numeric>
#include <cmath>
#include <vector>

namespace Chimera {

void RollingEdgeAudit::recordTrade(const TradeRecord& trade) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& q = trades_by_profile_[trade.profile];
    q.push_back(trade);

    while (q.size() > max_trades_)
        q.pop_front();
}

void RollingEdgeAudit::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    trades_by_profile_.clear();
}

size_t RollingEdgeAudit::tradeCount(const std::string& profile) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = trades_by_profile_.find(profile);
    if (it == trades_by_profile_.end()) return 0;
    return it->second.size();
}

RollingEdgeReport RollingEdgeAudit::evaluateProfile(const std::string& profile) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = trades_by_profile_.find(profile);
    if (it == trades_by_profile_.end()) {
        RollingEdgeReport empty;
        empty.profile = profile;
        return empty;
    }

    return computeReport(profile, it->second);
}

std::unordered_map<std::string, RollingEdgeReport> RollingEdgeAudit::evaluateAll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::unordered_map<std::string, RollingEdgeReport> results;
    for (const auto& [profile, trades] : trades_by_profile_) {
        results[profile] = computeReport(profile, trades);
    }
    return results;
}

RollingEdgeReport RollingEdgeAudit::computeReport(
    const std::string& profile,
    const std::deque<TradeRecord>& trades
) const {
    RollingEdgeReport r;
    r.profile = profile;
    r.trade_count = static_cast<int>(trades.size());

    if (trades.empty())
        return r;

    double sum_entry_edge = 0.0;
    double sum_exit_edge = 0.0;
    double sum_pnl = 0.0;

    int wins = 0;
    int losses = 0;

    double peak_equity = 0.0;
    double equity = 0.0;
    double max_dd = 0.0;

    std::vector<double> win_pnls;
    std::vector<double> loss_pnls;

    for (const auto& t : trades) {
        sum_entry_edge += t.entry_edge;
        sum_exit_edge += t.exit_edge;
        sum_pnl += t.pnl_r;

        equity += t.pnl_r;
        peak_equity = std::max(peak_equity, equity);
        max_dd = std::min(max_dd, equity - peak_equity);

        if (t.pnl_r > 0) {
            wins++;
            win_pnls.push_back(t.pnl_r);
        } else if (t.pnl_r < 0) {
            losses++;
            loss_pnls.push_back(std::abs(t.pnl_r));
        }
    }

    double n = static_cast<double>(trades.size());
    
    r.avg_edge_entry = sum_entry_edge / n;
    r.avg_edge_exit  = sum_exit_edge / n;
    r.edge_retention = (r.avg_edge_entry > 0.0)
        ? r.avg_edge_exit / r.avg_edge_entry
        : 0.0;

    r.win_rate = static_cast<double>(wins) / n;

    double avg_win = win_pnls.empty() ? 0.0 :
        std::accumulate(win_pnls.begin(), win_pnls.end(), 0.0) / 
        static_cast<double>(win_pnls.size());

    double avg_loss = loss_pnls.empty() ? 0.0 :
        std::accumulate(loss_pnls.begin(), loss_pnls.end(), 0.0) / 
        static_cast<double>(loss_pnls.size());

    r.payoff_ratio = (avg_loss > 0.0) ? avg_win / avg_loss : 0.0;

    r.avg_pnl_r = sum_pnl / n;
    r.max_drawdown_r = std::abs(max_dd);

    // =========================================================================
    // VERDICT LOGIC (AUTHORITATIVE)
    // =========================================================================
    
    // BROKEN: Edge is dead, profile must be disabled
    if (r.edge_retention < 0.55 ||
        r.payoff_ratio < 1.3 ||
        r.max_drawdown_r > 3.0) {
        r.verdict = RollingEdgeVerdict::BROKEN;
    }
    // DEGRADING: Edge is weakening, profile should be throttled
    else if (r.edge_retention < 0.65 ||
             r.payoff_ratio < 1.5 ||
             r.avg_pnl_r < 0.0) {
        r.verdict = RollingEdgeVerdict::DEGRADING;
    }
    // HEALTHY: Edge is alive
    else {
        r.verdict = RollingEdgeVerdict::HEALTHY;
    }

    return r;
}

} // namespace Chimera
