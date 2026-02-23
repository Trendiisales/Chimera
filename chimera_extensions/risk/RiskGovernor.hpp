#pragma once

#include <optional>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace chimera {
namespace risk {

enum class MetalSymbol {
    XAUUSD,
    XAGUSD
};

enum class TradeSide {
    BUY,
    SELL,
    NONE
};

enum class EngineType {
    HFT,
    STRUCTURE
};

struct AllocatedIntent {
    bool valid = false;
    MetalSymbol symbol;
    TradeSide side = TradeSide::NONE;
    double quantity = 0.0;
    bool is_exit = false;
    EngineType source_engine;
    double confidence = 0.0;
};

struct PositionRiskState {
    bool active = false;
    TradeSide side = TradeSide::NONE;
    double quantity = 0.0;
    double entry_price = 0.0;
    double current_price = 0.0;
};

struct GlobalRiskMetrics {
    double equity = 0.0;
    double daily_pnl = 0.0;
    double unrealized_pnl = 0.0;
    int consecutive_losses = 0;
    double volatility_score = 0.0; // normalized 0–3+
    uint64_t session_start_ns = 0;
};

struct RiskGovernorConfig {
    double daily_drawdown_limit = 500.0;
    int max_consecutive_losses = 4;
    double volatility_kill_threshold = 2.0;
    double min_risk_scale_floor = 0.2;
    double max_risk_scale_ceiling = 1.0;
};

class RiskGovernor {
public:
    explicit RiskGovernor(const RiskGovernorConfig& config = RiskGovernorConfig{})
        : m_config(config)
    {
        reset_state();
    }

    void set_daily_drawdown_limit(double value) 
    { 
        m_config.daily_drawdown_limit = value; 
    }

    void set_max_consecutive_losses(int count) 
    { 
        m_config.max_consecutive_losses = count; 
    }

    void set_volatility_kill_threshold(double threshold) 
    { 
        m_config.volatility_kill_threshold = threshold; 
    }

    void update_risk_metrics(const GlobalRiskMetrics& metrics)
    {
        m_metrics = metrics;
        evaluate_global_state();
    }

    void update_position(MetalSymbol symbol, const PositionRiskState& state)
    {
        m_positions[symbol] = state;
    }

    std::optional<AllocatedIntent> filter(const AllocatedIntent& intent)
    {
        if (!intent.valid)
            return std::nullopt;

        // Always allow exit orders
        if (intent.is_exit)
            return intent;

        // Hard stops
        if (m_trading_halted)
            return std::nullopt;

        if (m_volatility_locked)
            return std::nullopt;

        // Daily drawdown hard stop
        if (m_metrics.daily_pnl <= -m_config.daily_drawdown_limit)
        {
            m_trading_halted = true;
            return std::nullopt;
        }

        // Consecutive loss throttle
        if (m_metrics.consecutive_losses >= m_config.max_consecutive_losses)
            return std::nullopt;

        // Apply adaptive risk scaling
        const double scale_factor = calculate_risk_scale_factor();

        AllocatedIntent adjusted = intent;
        adjusted.quantity *= scale_factor;

        if (adjusted.quantity <= 0.0)
            return std::nullopt;

        return adjusted;
    }

    bool is_trading_halted() const { return m_trading_halted; }
    bool is_volatility_locked() const { return m_volatility_locked; }
    double current_risk_scale() const { return calculate_risk_scale_factor(); }
    
    const RiskGovernorConfig& config() const { return m_config; }
    const GlobalRiskMetrics& metrics() const { return m_metrics; }

    void reset_daily_state()
    {
        m_trading_halted = false;
        m_volatility_locked = false;
    }

private:
    RiskGovernorConfig m_config;
    GlobalRiskMetrics m_metrics{};

    std::unordered_map<MetalSymbol, PositionRiskState> m_positions;

    bool m_trading_halted = false;
    bool m_volatility_locked = false;

private:
    void reset_state()
    {
        m_trading_halted = false;
        m_volatility_locked = false;
        m_positions.clear();
    }

    void evaluate_global_state()
    {
        // Volatility kill switch
        if (m_metrics.volatility_score > m_config.volatility_kill_threshold)
            m_volatility_locked = true;
        else
            m_volatility_locked = false;
    }

    double calculate_risk_scale_factor() const
    {
        // Component 1: Drawdown scaling
        double drawdown_scale = 1.0;
        if (m_config.daily_drawdown_limit > 0.0)
        {
            const double drawdown_ratio = std::clamp(
                std::abs(m_metrics.daily_pnl) / m_config.daily_drawdown_limit,
                0.0,
                1.0
            );
            drawdown_scale = 1.0 - drawdown_ratio;
        }

        // Component 2: Volatility scaling
        double volatility_scale = 1.0;
        if (m_metrics.volatility_score > 1.0)
        {
            volatility_scale = 1.0 / m_metrics.volatility_score;
        }

        // Component 3: Consecutive loss scaling
        double loss_scale = 1.0;
        if (m_metrics.consecutive_losses > 0)
        {
            const double loss_penalty = std::min(
                m_metrics.consecutive_losses * 0.15,
                0.6
            );
            loss_scale = 1.0 - loss_penalty;
        }

        // Combine all factors
        const double combined_scale = drawdown_scale * volatility_scale * loss_scale;

        return std::clamp(
            combined_scale,
            m_config.min_risk_scale_floor,
            m_config.max_risk_scale_ceiling
        );
    }
};

} // namespace risk
} // namespace chimera
