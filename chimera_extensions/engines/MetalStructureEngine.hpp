#pragma once

#include <deque>
#include <cmath>
#include <cstdint>
#include <optional>
#include <algorithm>
#include <string>

namespace chimera {
namespace engines {

enum class MetalSymbol {
    XAUUSD,
    XAGUSD
};

enum class TradeSide {
    BUY,
    SELL,
    NONE
};

enum class StructureState {
    FLAT,
    SETUP,
    ENTERED,
    HOLD,
    TRAIL,
    COOLDOWN
};

struct MarketSnapshot {
    double bid;
    double ask;
    double mid;
    double ofi;          // order flow imbalance
    double spread;
    uint64_t timestamp_ns;
};

struct StructureIntent {
    bool valid = false;
    MetalSymbol symbol;
    TradeSide side = TradeSide::NONE;
    double quantity = 0.0;
    double confidence = 0.0;
    bool is_exit = false;
};

struct PositionUpdate {
    bool filled = false;
    bool closed = false;
    double fill_price = 0.0;
    double quantity = 0.0;
    uint64_t timestamp_ns = 0;
};

class MetalStructureEngine {
public:
    explicit MetalStructureEngine(MetalSymbol sym)
        : m_symbol(sym)
    {
        reset();
    }

    void on_market_tick(const MarketSnapshot& snapshot)
    {
        m_last_snapshot = snapshot;
        update_price_windows(snapshot);
        update_indicators();
        update_state_machine();
    }

    void on_position_update(const PositionUpdate& update)
    {
        if (!m_position_active) return;

        if (update.closed) {
            m_position_active = false;
            m_state = StructureState::COOLDOWN;
            m_cooldown_start_ts = m_last_snapshot.timestamp_ns;
        }
    }

    std::optional<StructureIntent> poll_intent()
    {
        if (!m_pending_intent.valid)
            return std::nullopt;

        auto out = m_pending_intent;
        m_pending_intent = StructureIntent{};
        return out;
    }

    StructureState current_state() const { return m_state; }
    bool has_position() const { return m_position_active; }
    TradeSide position_side() const { return m_position_side; }
    double unrealized_pnl_bps() const { return calculate_unrealized_bps(); }

private:
    // ==================== CONFIGURATION ====================
    
    const double m_base_quantity = 1.0;

    double trend_entry_threshold() const {
        return m_symbol == MetalSymbol::XAUUSD ? 0.65 : 0.70;
    }

    double ofi_entry_threshold() const {
        return m_symbol == MetalSymbol::XAUUSD ? 0.60 : 0.65;
    }

    double min_stop_bps() const {
        return m_symbol == MetalSymbol::XAUUSD ? 5.0 : 7.0;
    }

    double trail_trigger_bps() const {
        return m_symbol == MetalSymbol::XAUUSD ? 6.0 : 8.0;
    }

    double max_hold_minutes() const {
        return m_symbol == MetalSymbol::XAUUSD ? 45.0 : 30.0;
    }

    double max_size_multiplier() const {
        return m_symbol == MetalSymbol::XAUUSD ? 3.0 : 2.0;
    }

private:
    // ==================== STATE ====================
    
    MetalSymbol m_symbol;
    StructureState m_state{StructureState::FLAT};

    MarketSnapshot m_last_snapshot{};

    bool m_position_active = false;
    TradeSide m_position_side = TradeSide::NONE;
    double m_entry_price = 0.0;
    uint64_t m_entry_timestamp = 0;
    uint64_t m_cooldown_start_ts = 0;

    double m_trailing_stop = 0.0;

    StructureIntent m_pending_intent;

    // ==================== ROLLING WINDOWS ====================
    
    std::deque<double> m_mid_window_1m;   // 60 samples
    std::deque<double> m_mid_window_5m;   // 300 samples
    std::deque<double> m_ofi_window;      // 120 samples

    double m_ema_fast = 0.0;
    double m_ema_slow = 0.0;

private:
    // ==================== CORE LOGIC ====================
    
    void reset()
    {
        m_state = StructureState::FLAT;
        m_position_active = false;
        m_position_side = TradeSide::NONE;
        m_entry_price = 0.0;
        m_trailing_stop = 0.0;
    }

    void update_price_windows(const MarketSnapshot& snapshot)
    {
        m_mid_window_1m.push_back(snapshot.mid);
        if (m_mid_window_1m.size() > 60)
            m_mid_window_1m.pop_front();

        m_mid_window_5m.push_back(snapshot.mid);
        if (m_mid_window_5m.size() > 300)
            m_mid_window_5m.pop_front();

        m_ofi_window.push_back(snapshot.ofi);
        if (m_ofi_window.size() > 120)
            m_ofi_window.pop_front();
    }

    void update_indicators()
    {
        if (m_mid_window_1m.size() < 10)
            return;

        const double alpha_fast = 2.0 / (8.0 + 1.0);
        const double alpha_slow = 2.0 / (21.0 + 1.0);

        m_ema_fast = alpha_fast * m_mid_window_1m.back()
                   + (1.0 - alpha_fast) * m_ema_fast;

        m_ema_slow = alpha_slow * m_mid_window_1m.back()
                   + (1.0 - alpha_slow) * m_ema_slow;
    }

    double calculate_trend_score() const
    {
        if (m_ema_slow == 0.0) return 0.0;
        const double slope = m_ema_fast - m_ema_slow;
        const double normalized = std::min(std::abs(slope) * 1000.0, 1.0);
        return normalized;
    }

    TradeSide trend_direction() const
    {
        if (m_ema_fast > m_ema_slow) return TradeSide::BUY;
        if (m_ema_fast < m_ema_slow) return TradeSide::SELL;
        return TradeSide::NONE;
    }

    double calculate_ofi_persistence() const
    {
        if (m_ofi_window.empty()) return 0.0;

        int aligned_count = 0;
        const TradeSide direction = trend_direction();

        for (double ofi_value : m_ofi_window) {
            if ((direction == TradeSide::BUY && ofi_value > 0) ||
                (direction == TradeSide::SELL && ofi_value < 0))
                aligned_count++;
        }

        return static_cast<double>(aligned_count) / m_ofi_window.size();
    }

    double calculate_unrealized_bps() const
    {
        if (!m_position_active) return 0.0;

        double price_diff = m_last_snapshot.mid - m_entry_price;
        if (m_position_side == TradeSide::SELL)
            price_diff = -price_diff;

        return (price_diff / m_entry_price) * 10000.0;
    }

    void update_state_machine()
    {
        switch (m_state) {

        case StructureState::FLAT:
            attempt_entry();
            break;

        case StructureState::ENTERED:
            manage_entered_position();
            break;

        case StructureState::HOLD:
            manage_hold_position();
            break;

        case StructureState::TRAIL:
            manage_trailing_position();
            break;

        case StructureState::COOLDOWN:
            if (m_last_snapshot.timestamp_ns - m_cooldown_start_ts > 60000000000ULL) // 60s in ns
                m_state = StructureState::FLAT;
            break;

        default:
            break;
        }
    }

    void attempt_entry()
    {
        const double trend_score = calculate_trend_score();
        const double ofi_persistence = calculate_ofi_persistence();

        if (trend_score < trend_entry_threshold())
            return;

        if (ofi_persistence < ofi_entry_threshold())
            return;

        const TradeSide entry_direction = trend_direction();
        if (entry_direction == TradeSide::NONE)
            return;

        const double size_multiplier = 1.0 + 1.5 * trend_score + 1.0 * ofi_persistence;
        const double final_quantity = m_base_quantity * std::min(size_multiplier, max_size_multiplier());

        m_pending_intent = {
            true,
            m_symbol,
            entry_direction,
            final_quantity,
            (trend_score + ofi_persistence) / 2.0,
            false
        };

        m_position_active = true;
        m_position_side = entry_direction;
        m_entry_price = m_last_snapshot.mid;
        m_entry_timestamp = m_last_snapshot.timestamp_ns;

        m_state = StructureState::ENTERED;
    }

    void manage_entered_position()
    {
        const double profit_bps = calculate_unrealized_bps();

        if (profit_bps > trail_trigger_bps()) {
            m_state = StructureState::HOLD;
        }

        if (profit_bps < -min_stop_bps()) {
            execute_exit();
        }
    }

    void manage_hold_position()
    {
        const double profit_bps = calculate_unrealized_bps();

        if (profit_bps > trail_trigger_bps()) {
            m_trailing_stop = m_last_snapshot.mid;
            m_state = StructureState::TRAIL;
        }

        if (profit_bps < -min_stop_bps())
            execute_exit();
    }

    void manage_trailing_position()
    {
        const double profit_bps = calculate_unrealized_bps();

        if (m_position_side == TradeSide::BUY)
            m_trailing_stop = std::max(m_trailing_stop, m_last_snapshot.mid);
        else
            m_trailing_stop = std::min(m_trailing_stop, m_last_snapshot.mid);

        double retrace_bps = 0.0;
        if (m_position_side == TradeSide::BUY)
            retrace_bps = (m_trailing_stop - m_last_snapshot.mid) / m_entry_price * 10000.0;
        else
            retrace_bps = (m_last_snapshot.mid - m_trailing_stop) / m_entry_price * 10000.0;

        if (retrace_bps > trail_trigger_bps() / 2.0)
            execute_exit();

        const uint64_t holding_time_ns = m_last_snapshot.timestamp_ns - m_entry_timestamp;
        const uint64_t max_hold_ns = static_cast<uint64_t>(max_hold_minutes() * 60.0 * 1e9);
        
        if (holding_time_ns > max_hold_ns)
            execute_exit();
    }

    void execute_exit()
    {
        if (!m_position_active) return;

        m_pending_intent = {
            true,
            m_symbol,
            m_position_side,
            0.0,
            1.0,
            true
        };

        m_position_active = false;
        m_state = StructureState::COOLDOWN;
        m_cooldown_start_ts = m_last_snapshot.timestamp_ns;
    }
};

} // namespace engines
} // namespace chimera
