#pragma once

#include "../core/OrderIntentTypes.hpp"
#include <atomic>
#include <chrono>
#include <mutex>

namespace chimera {
namespace risk {

enum class SessionType {
    ASIA,
    LONDON,
    NEWYORK,
    DEAD
};

struct RiskDecision {
    bool approved = false;
    double size_multiplier = 1.0;
};

class RiskGovernorV3 {
public:
    RiskGovernorV3(double max_daily_loss, double base_spread_limit, 
                   double base_vol_limit, double latency_limit_ms);

    RiskDecision evaluate(const core::OrderIntent& intent);
    
    void update_market_state(double spread, double volatility, double latency_ms);
    void record_fill(double pnl);
    void record_reject();
    void record_latency(double latency_ms);
    void reset_daily();
    void set_kill_switch(bool state);
    
    bool is_lockdown_active() const { return m_lockdown_mode.load(); }

private:
    SessionType detect_session();
    double compute_spread_threshold(SessionType s);
    double compute_vol_threshold(SessionType s);
    double compute_latency_threshold(SessionType s);
    double compute_drawdown_multiplier();
    double compute_reject_penalty();
    double compute_latency_penalty();
    double compute_volatility_penalty();
    
    // FIX #6: Volatility lockdown logic
    void check_lockdown_conditions();

    double m_max_daily_loss;
    double m_base_spread_limit;
    double m_base_vol_limit;
    double m_base_latency_limit;

    std::atomic<double> m_daily_pnl{0.0};
    std::atomic<int> m_reject_count{0};
    std::atomic<bool> m_kill_switch{false};
    std::atomic<bool> m_lockdown_mode{false}; // FIX #6
    std::atomic<double> m_current_spread{0.0};
    std::atomic<double> m_current_vol{0.0};
    std::atomic<double> m_current_latency{0.0};
    std::atomic<double> m_latency_ema{0.0};

    std::chrono::steady_clock::time_point m_session_anchor;
    std::mutex m_mutex;
};

} // namespace risk
} // namespace chimera
