#pragma once

#include <unordered_map>
#include <string>
#include <mutex>

namespace chimera {

class PnLGovernor {
public:
    struct StrategyStats {
        // Core PnL
        double realized_pnl{0.0};
        double rolling_ev{0.0};
        bool   killed{false};
        
        // PRO-GRADE METRICS
        int    fills{0};
        int    wins{0};
        int    losses{0};
        double total_win{0.0};
        double total_loss{0.0};
        double avg_edge_bps{0.0};
        double avg_fee_bps{10.0};
        double avg_slip_bps{0.0};
        double avg_lat_bps{0.0};
        double maker_fills{0};
        double taker_fills{0};
        double total_notional{0.0};
    };

    PnLGovernor();

    void set_strategy_floor(double ev_floor);
    void set_portfolio_dd(double max_loss);
    
    // Enhanced fill tracking with execution quality
    void update_fill(const std::string& strategy, double pnl_delta, 
                    double edge_bps = 0.0, double fee_bps = 10.0,
                    double slip_bps = 0.0, double lat_bps = 0.0,
                    double notional = 0.0, bool is_maker = true);
    
    bool allow_strategy(const std::string& strategy) const;
    void block_engine(const std::string& engine_id);
    
    double portfolio_pnl() const;
    bool   portfolio_killed() const;
    
    std::unordered_map<std::string, StrategyStats> dump_stats() const;
    void reset();

private:
    void update_ev(StrategyStats& stats, double delta);

    double ev_floor_{-10.0};
    double portfolio_dd_{-500.0};

    mutable std::mutex mtx_;
    std::unordered_map<std::string, StrategyStats> stats_;

    double portfolio_pnl_{0.0};
    bool   portfolio_killed_{false};
};

} // namespace chimera
