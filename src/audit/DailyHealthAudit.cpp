// =============================================================================
// DailyHealthAudit.cpp - v4.8.0 - DAILY HEALTH AUDIT IMPLEMENTATION
// =============================================================================
#include "audit/DailyHealthAudit.hpp"

#include <algorithm>
#include <numeric>
#include <cmath>

namespace Chimera {

void DailyHealthAudit::recordTrade(const TradeRecord& trade) {
    std::lock_guard<std::mutex> lock(mutex_);
    trades_.push_back(trade);
}

void DailyHealthAudit::recordVeto(const std::string& /*symbol*/, const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    vetoes_.push_back(reason);
}

void DailyHealthAudit::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    trades_.clear();
    vetoes_.clear();
}

int DailyHealthAudit::countWins() const {
    int count = 0;
    for (const auto& t : trades_) {
        if (t.pnl_r > 0.0) count++;
    }
    return count;
}

int DailyHealthAudit::countLosses() const {
    int count = 0;
    for (const auto& t : trades_) {
        if (t.pnl_r < 0.0) count++;
    }
    return count;
}

int DailyHealthAudit::countScratches() const {
    int count = 0;
    for (const auto& t : trades_) {
        if (std::abs(t.pnl_r) < 0.05) count++;  // Within 0.05R = scratch
    }
    return count;
}

double DailyHealthAudit::computeAvgLoss() const {
    std::vector<double> losses;
    for (const auto& t : trades_) {
        if (t.pnl_r < 0)
            losses.push_back(std::abs(t.pnl_r));
    }
    if (losses.empty()) return 0.0;
    return std::accumulate(losses.begin(), losses.end(), 0.0) / static_cast<double>(losses.size());
}

double DailyHealthAudit::computeAvgWin() const {
    std::vector<double> wins;
    for (const auto& t : trades_) {
        if (t.pnl_r > 0)
            wins.push_back(t.pnl_r);
    }
    if (wins.empty()) return 0.0;
    return std::accumulate(wins.begin(), wins.end(), 0.0) / static_cast<double>(wins.size());
}

double DailyHealthAudit::computePayoffRatio() const {
    double avg_loss = computeAvgLoss();
    if (avg_loss == 0.0) return 0.0;
    return computeAvgWin() / avg_loss;
}

double DailyHealthAudit::computeAvgLosingDuration() const {
    std::vector<double> d;
    for (const auto& t : trades_) {
        if (t.pnl_r < 0)
            d.push_back(static_cast<double>(t.duration.count()) / 1000.0);
    }
    if (d.empty()) return 0.0;
    return std::accumulate(d.begin(), d.end(), 0.0) / static_cast<double>(d.size());
}

double DailyHealthAudit::computeAvgWinningDuration() const {
    std::vector<double> d;
    for (const auto& t : trades_) {
        if (t.pnl_r > 0)
            d.push_back(static_cast<double>(t.duration.count()) / 1000.0);
    }
    if (d.empty()) return 0.0;
    return std::accumulate(d.begin(), d.end(), 0.0) / static_cast<double>(d.size());
}

double DailyHealthAudit::computeMaxTradeLoss() const {
    double worst = 0.0;
    for (const auto& t : trades_) {
        if (t.pnl_r < worst)
            worst = t.pnl_r;
    }
    return std::abs(worst);
}

double DailyHealthAudit::computeWorstThreeTradeDD() const {
    if (trades_.size() < 3) return 0.0;

    double worst_dd = 0.0;
    for (size_t i = 0; i + 2 < trades_.size(); ++i) {
        double dd = trades_[i].pnl_r +
                    trades_[i + 1].pnl_r +
                    trades_[i + 2].pnl_r;
        worst_dd = std::min(worst_dd, dd);
    }
    return std::abs(worst_dd);
}

bool DailyHealthAudit::vetoReasonsSane() const {
    for (const auto& v : vetoes_) {
        if (v.empty()) return false;
        if (v == "UNKNOWN" || v == "DEFAULT" || v == "FALLBACK")
            return false;
    }
    return true;
}

DailyAuditReport DailyHealthAudit::runDailyAudit() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    DailyAuditReport r;

    // Compute all metrics
    r.avg_loss_r = computeAvgLoss();
    r.avg_win_r = computeAvgWin();
    r.payoff_ratio = computePayoffRatio();

    r.avg_losing_duration_sec = computeAvgLosingDuration();
    r.avg_winning_duration_sec = computeAvgWinningDuration();

    r.max_trade_loss_r = computeMaxTradeLoss();
    r.worst_three_trade_dd_r = computeWorstThreeTradeDD();

    r.veto_reasons = vetoes_;
    
    // Trade counts
    r.total_trades = static_cast<int>(trades_.size());
    r.winning_trades = countWins();
    r.losing_trades = countLosses();
    r.scratch_trades = countScratches();
    
    // Win rate
    if (r.total_trades > 0) {
        r.win_rate = static_cast<double>(r.winning_trades) / static_cast<double>(r.total_trades);
    }

    // =========================================================================
    // HARD RULES (NON-NEGOTIABLE)
    // =========================================================================
    
    // Rule 1: Average loss must be <= 1R
    if (r.avg_loss_r > 1.0) {
        r.fail = true;
    }
    
    // Rule 2: Payoff ratio must be >= 1.5 (if there are wins)
    if (r.payoff_ratio < 1.5 && r.avg_win_r > 0.0) {
        r.fail = true;
    }
    
    // Rule 3: No single trade can lose more than 1.2R
    if (r.max_trade_loss_r > 1.2) {
        r.fail = true;
    }
    
    // Rule 4: Worst 3-trade drawdown must be <= 2R
    if (r.worst_three_trade_dd_r > 2.0) {
        r.fail = true;
    }
    
    // Rule 5: Losing trades should not take longer than 50% of winning trades
    if (r.avg_winning_duration_sec > 0 &&
        r.avg_losing_duration_sec > 0.5 * r.avg_winning_duration_sec) {
        r.warning = true;
    }
    
    // Rule 6: Veto reasons must make sense
    if (!vetoReasonsSane()) {
        r.fail = true;
    }

    // =========================================================================
    // FINAL VERDICT
    // =========================================================================
    if (r.fail) {
        r.pass = false;
        r.verdict = "FAIL";
    } else if (r.warning) {
        r.pass = false;
        r.verdict = "WARNING";
    } else {
        r.pass = true;
        r.verdict = "PASS";
    }

    return r;
}

} // namespace Chimera
