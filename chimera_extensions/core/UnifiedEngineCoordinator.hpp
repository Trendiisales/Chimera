#pragma once

#include <optional>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include "MetalStructureEngine.hpp"
#include "EnhancedCapitalAllocator.hpp"
#include "RiskGovernor.hpp"
#include "TelemetryCollector.hpp"

namespace chimera {
namespace core {

using namespace chimera::engines;
using namespace chimera::allocation;
using namespace chimera::risk;
using namespace chimera::telemetry;

struct CoordinatorConfig {
    AllocationConfig allocation;
    RiskGovernorConfig risk;
    TelemetryConfig telemetry;
};

struct MarketTickEvent {
    MetalSymbol symbol;
    double bid;
    double ask;
    double mid;
    double ofi;
    double spread;
    uint64_t timestamp_ns;
};

struct ExecutionEvent {
    MetalSymbol symbol;
    TradeSide side;
    double quantity;
    double fill_price;
    uint64_t send_timestamp_ns;
    uint64_t ack_timestamp_ns;
    uint64_t fill_timestamp_ns;
    bool is_close;
    bool filled;
};

struct HFTEngineIntent {
    bool valid = false;
    MetalSymbol symbol;
    TradeSide side = TradeSide::NONE;
    double quantity = 0.0;
    double confidence = 0.0;
    bool is_exit = false;
};

class UnifiedEngineCoordinator {
public:
    explicit UnifiedEngineCoordinator(const CoordinatorConfig& config = CoordinatorConfig{})
        : m_xau_structure_engine(MetalSymbol::XAUUSD),
          m_xag_structure_engine(MetalSymbol::XAGUSD),
          m_allocator(config.allocation),
          m_risk_governor(config.risk),
          m_telemetry(config.telemetry)
    {
    }

    // ==================== MARKET DATA INPUT ====================
    
    void on_market_tick(const MarketTickEvent& tick)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Convert to internal format
        MarketSnapshot snapshot{
            tick.bid,
            tick.ask,
            tick.mid,
            tick.ofi,
            tick.spread,
            tick.timestamp_ns
        };

        // Route to appropriate structure engine
        if (tick.symbol == MetalSymbol::XAUUSD)
            m_xau_structure_engine.on_market_tick(snapshot);
        else if (tick.symbol == MetalSymbol::XAGUSD)
            m_xag_structure_engine.on_market_tick(snapshot);

        // Update telemetry
        m_telemetry.on_market_tick(tick);

        // Store last tick
        m_last_ticks[tick.symbol] = tick;
    }

    // ==================== EXECUTION FEEDBACK ====================
    
    void on_execution(const ExecutionEvent& execution)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Convert to internal format
        PositionUpdate update{
            execution.filled,
            execution.is_close,
            execution.fill_price,
            execution.quantity,
            execution.fill_timestamp_ns
        };

        // Route to appropriate structure engine
        if (execution.symbol == MetalSymbol::XAUUSD)
            m_xau_structure_engine.on_position_update(update);
        else if (execution.symbol == MetalSymbol::XAGUSD)
            m_xag_structure_engine.on_position_update(update);

        // Update telemetry
        m_telemetry.on_execution(execution);

        // Update allocator position state
        update_allocator_position(execution);
    }

    // ==================== RISK METRICS INPUT ====================
    
    void update_risk_metrics(const GlobalRiskMetrics& metrics)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_risk_governor.update_risk_metrics(metrics);
    }

    // ==================== INTENT PROCESSING ====================
    
    std::optional<AllocatedIntent> process_intents(const HFTEngineIntent& hft_intent)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Poll structure engine intents
        auto xau_structure_intent = m_xau_structure_engine.poll_intent();
        auto xag_structure_intent = m_xag_structure_engine.poll_intent();

        // Convert structure intents to allocator format
        EngineIntent structure_signal{};
        
        if (xau_structure_intent)
            structure_signal = convert_structure_to_engine_intent(*xau_structure_intent);
        else if (xag_structure_intent)
            structure_signal = convert_structure_to_engine_intent(*xag_structure_intent);

        // Convert HFT intent to allocator format
        EngineIntent hft_signal = convert_hft_to_engine_intent(hft_intent);

        // Capital allocation
        auto allocated = m_allocator.allocate(hft_signal, structure_signal);
        if (!allocated)
            return std::nullopt;

        // Risk filtering
        auto filtered = m_risk_governor.filter(*allocated);
        if (!filtered)
            return std::nullopt;

        // Log approved intent
        m_telemetry.on_intent_approved(*filtered);

        return filtered;
    }

    // ==================== STATE QUERIES ====================
    
    bool has_xau_position() const 
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_xau_structure_engine.has_position();
    }

    bool has_xag_position() const 
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_xag_structure_engine.has_position();
    }

    StructureState get_xau_state() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_xau_structure_engine.current_state();
    }

    StructureState get_xag_state() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_xag_structure_engine.current_state();
    }

    bool is_trading_halted() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_risk_governor.is_trading_halted();
    }

    double get_risk_scale() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_risk_governor.current_risk_scale();
    }

    TelemetrySnapshot get_telemetry_snapshot() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_telemetry.create_snapshot();
    }

private:
    mutable std::mutex m_mutex;

    // Engine components
    MetalStructureEngine m_xau_structure_engine;
    MetalStructureEngine m_xag_structure_engine;

    // Control components
    EnhancedCapitalAllocator m_allocator;
    RiskGovernor m_risk_governor;
    TelemetryCollector m_telemetry;

    // State tracking
    std::unordered_map<MetalSymbol, MarketTickEvent> m_last_ticks;

private:
    EngineIntent convert_structure_to_engine_intent(const StructureIntent& intent)
    {
        return EngineIntent{
            intent.valid,
            EngineType::STRUCTURE,
            intent.symbol,
            intent.side,
            intent.quantity,
            intent.confidence,
            intent.is_exit
        };
    }

    EngineIntent convert_hft_to_engine_intent(const HFTEngineIntent& intent)
    {
        return EngineIntent{
            intent.valid,
            EngineType::HFT,
            intent.symbol,
            intent.side,
            intent.quantity,
            intent.confidence,
            intent.is_exit
        };
    }

    void update_allocator_position(const ExecutionEvent& execution)
    {
        PositionState state{
            !execution.is_close,
            execution.side,
            execution.quantity,
            execution.fill_price,
            execution.fill_timestamp_ns
        };

        m_allocator.update_position_state(execution.symbol, state);
    }
};

} // namespace core
} // namespace chimera
