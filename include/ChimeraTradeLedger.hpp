#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <cmath>

namespace omega {

// Realistic cost model for shadow simulation.
// All values are in the same USD PnL domain used by the main loop.
struct TradeRecord
{
    int         id          = 0;
    std::string symbol;
    std::string side;
    double      entryPrice  = 0;
    double      exitPrice   = 0;
    double      tp          = 0;
    double      sl          = 0;
    double      size        = 1.0;
    double      pnl         = 0;   // gross pnl before costs
    double      mfe         = 0;
    double      mae         = 0;
    int64_t     entryTs     = 0;
    int64_t     exitTs      = 0;
    std::string exitReason;
    double      spreadAtEntry = 0;
    double      latencyMs   = 0;
    std::string engine      = "BreakoutEngine";
    std::string regime;

    // Cost model outputs.
    double      slippage_entry  = 0;
    double      slippage_exit   = 0;
    double      commission      = 0;
    double      net_pnl         = 0;

    // Cost parameters applied.
    double      slip_entry_pct  = 0;
    double      slip_exit_pct   = 0;
    double      comm_per_side   = 0;
};

inline void apply_realistic_costs(TradeRecord& tr,
                                  double commission_per_side,
                                  double tick_mult)
{
    tr.slip_entry_pct = 0.0;
    tr.slip_exit_pct  = 0.0;
    tr.comm_per_side  = commission_per_side;

    tr.slippage_entry = (tr.spreadAtEntry / 2.0) * tick_mult * tr.size;
    tr.slippage_exit  = 0.0;
    tr.commission     = commission_per_side * 2.0 * tr.size;
    tr.net_pnl        = tr.pnl - tr.slippage_entry - tr.slippage_exit - tr.commission;
}

class ChimeraTradeLedger
{
public:
    void record(const TradeRecord& tr)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_trades.push_back(tr);

        const double pnl_for_stats = (tr.net_pnl != 0.0 || tr.slippage_entry != 0.0)
                                     ? tr.net_pnl : tr.pnl;
        if (pnl_for_stats > 0) {
            m_wins++;
            m_sum_win += pnl_for_stats;
        } else {
            m_losses++;
            m_sum_loss += std::abs(pnl_for_stats);
        }
        m_daily_pnl       += pnl_for_stats;
        m_cumulative_pnl  += pnl_for_stats;
        m_gross_daily_pnl += tr.pnl;
        if (m_daily_pnl - m_peak_pnl < -m_max_dd) m_max_dd = m_peak_pnl - m_daily_pnl;
        if (m_daily_pnl > m_peak_pnl) m_peak_pnl = m_daily_pnl;
    }

    std::vector<TradeRecord> snapshot() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_trades;
    }

    double dailyPnl() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_daily_pnl;
    }
    double cumulativePnl() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_cumulative_pnl;
    }
    double grossDailyPnl() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_gross_daily_pnl;
    }
    double maxDD() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_max_dd;
    }
    int wins() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_wins;
    }
    int losses() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_losses;
    }
    int total() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_wins + m_losses;
    }
    double winRate() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        const int t = m_wins + m_losses;
        return t > 0 ? (100.0 * m_wins / t) : 0.0;
    }
    double avgWin() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_wins > 0 ? (m_sum_win / m_wins) : 0.0;
    }
    double avgLoss() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_losses > 0 ? (m_sum_loss / m_losses) : 0.0;
    }

    void resetDaily()
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_trades.clear();
        m_daily_pnl = 0;
        m_gross_daily_pnl = 0;
        m_peak_pnl = 0;
        m_max_dd = 0;
        m_wins = 0;
        m_losses = 0;
        m_sum_win = 0;
        m_sum_loss = 0;
    }

private:
    mutable std::mutex        m_mtx;
    std::vector<TradeRecord>  m_trades;
    double m_daily_pnl       = 0;
    double m_gross_daily_pnl = 0;
    double m_cumulative_pnl  = 0;
    double m_peak_pnl        = 0;
    double m_max_dd          = 0;
    int    m_wins            = 0;
    int    m_losses          = 0;
    double m_sum_win         = 0;
    double m_sum_loss        = 0;
};

} // namespace omega
