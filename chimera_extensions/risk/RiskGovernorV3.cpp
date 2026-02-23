#include "RiskGovernorV3.hpp"
#include <cmath>
#include <algorithm>
#include <ctime>

namespace chimera {
namespace risk {

RiskGovernorV3::RiskGovernorV3(double max_daily_loss, double base_spread_limit,
                               double base_vol_limit, double latency_limit_ms)
    : m_max_daily_loss(max_daily_loss)
    , m_base_spread_limit(base_spread_limit)
    , m_base_vol_limit(base_vol_limit)
    , m_base_latency_limit(latency_limit_ms)
    , m_session_anchor(std::chrono::steady_clock::now()) {}

// FIX #6: CRITICAL - Global volatility lockdown
void RiskGovernorV3::check_lockdown_conditions() {
    double vol = m_current_vol.load();
    double latency = m_latency_ema.load();
    
    // Trigger lockdown if:
    // 1. Volatility spikes 2x above normal
    // 2. Latency spikes 2x above normal
    if (vol > m_base_vol_limit * 2.0 || latency > m_base_latency_limit * 2.0) {
        m_lockdown_mode.store(true);
    } else {
        // Exit lockdown when conditions normalize
        if (vol < m_base_vol_limit * 1.5 && latency < m_base_latency_limit * 1.5) {
            m_lockdown_mode.store(false);
        }
    }
}

RiskDecision RiskGovernorV3::evaluate(const core::OrderIntent& intent) {
    RiskDecision decision;
    
    // FIX #6: Lockdown mode forces drastic size reduction
    if (m_lockdown_mode.load()) {
        decision.approved = true;  // Still allow trading
        decision.size_multiplier = 0.2;  // But only 20% size
        return decision;
    }
    
    if (m_kill_switch.load())
        return decision;
    
    if (m_daily_pnl.load() <= -m_max_daily_loss)
        return decision;
    
    SessionType session = detect_session();
    
    double spread_limit = compute_spread_threshold(session);
    double vol_limit = compute_vol_threshold(session);
    double latency_limit = compute_latency_threshold(session);
    
    if (m_current_spread.load() > spread_limit)
        return decision;
    
    if (m_current_vol.load() > vol_limit)
        return decision;
    
    if (m_latency_ema.load() > latency_limit)
        return decision;
    
    if (m_reject_count.load() > 15)
        return decision;
    
    double multiplier = 1.0;
    multiplier *= compute_drawdown_multiplier();
    multiplier *= compute_reject_penalty();
    multiplier *= compute_latency_penalty();
    multiplier *= compute_volatility_penalty();
    multiplier = std::clamp(multiplier, 0.2, 1.5);
    
    decision.approved = true;
    decision.size_multiplier = multiplier;
    return decision;
}

void RiskGovernorV3::update_market_state(double spread, double volatility, double latency_ms) {
    m_current_spread.store(spread);
    m_current_vol.store(volatility);
    m_current_latency.store(latency_ms);
    
    double alpha = 0.1;
    double current_ema = m_latency_ema.load();
    m_latency_ema.store((alpha * latency_ms) + (1.0 - alpha) * current_ema);
    
    // FIX #6: Check lockdown after state update
    check_lockdown_conditions();
}

void RiskGovernorV3::record_fill(double pnl) {
    m_daily_pnl.fetch_add(pnl);
}

void RiskGovernorV3::record_reject() {
    m_reject_count.fetch_add(1);
}

void RiskGovernorV3::record_latency(double latency_ms) {
    update_market_state(m_current_spread.load(), m_current_vol.load(), latency_ms);
}

void RiskGovernorV3::reset_daily() {
    m_daily_pnl.store(0.0);
    m_reject_count.store(0);
    m_session_anchor = std::chrono::steady_clock::now();
}

void RiskGovernorV3::set_kill_switch(bool state) {
    m_kill_switch.store(state);
}

SessionType RiskGovernorV3::detect_session() {
    auto now = std::chrono::system_clock::now();
    time_t tt = std::chrono::system_clock::to_time_t(now);
    tm utc;
    
#ifdef _WIN32
    gmtime_s(&utc, &tt);
#else
    gmtime_r(&tt, &utc);
#endif
    
    int hour = utc.tm_hour;
    
    if (hour >= 0 && hour < 7)
        return SessionType::ASIA;
    if (hour >= 7 && hour < 13)
        return SessionType::LONDON;
    if (hour >= 13 && hour < 21)
        return SessionType::NEWYORK;
    return SessionType::DEAD;
}

double RiskGovernorV3::compute_spread_threshold(SessionType s) {
    switch (s) {
        case SessionType::LONDON: return m_base_spread_limit * 1.0;
        case SessionType::NEWYORK: return m_base_spread_limit * 1.1;
        case SessionType::ASIA: return m_base_spread_limit * 0.8;
        default: return m_base_spread_limit * 0.6;
    }
}

double RiskGovernorV3::compute_vol_threshold(SessionType s) {
    switch (s) {
        case SessionType::LONDON: return m_base_vol_limit * 1.2;
        case SessionType::NEWYORK: return m_base_vol_limit * 1.3;
        case SessionType::ASIA: return m_base_vol_limit * 0.9;
        default: return m_base_vol_limit * 0.7;
    }
}

double RiskGovernorV3::compute_latency_threshold(SessionType s) {
    switch (s) {
        case SessionType::LONDON: return m_base_latency_limit;
        case SessionType::NEWYORK: return m_base_latency_limit * 1.1;
        default: return m_base_latency_limit * 0.9;
    }
}

double RiskGovernorV3::compute_drawdown_multiplier() {
    double dd_ratio = std::max(0.0, -m_daily_pnl.load() / m_max_daily_loss);
    return 1.0 - (dd_ratio * 0.5);
}

double RiskGovernorV3::compute_reject_penalty() {
    return std::max(0.5, 1.0 - (m_reject_count.load() * 0.03));
}

double RiskGovernorV3::compute_latency_penalty() {
    double latency = m_latency_ema.load();
    if (latency < m_base_latency_limit * 0.5)
        return 1.1;
    if (latency > m_base_latency_limit)
        return 0.7;
    return 1.0;
}

double RiskGovernorV3::compute_volatility_penalty() {
    double vol = m_current_vol.load();
    if (vol > m_base_vol_limit * 0.8)
        return 0.8;
    if (vol < m_base_vol_limit * 0.5)
        return 1.1;
    return 1.0;
}

} // namespace risk
} // namespace chimera
