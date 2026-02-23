#pragma once

#include "UnifiedEngineCoordinator.hpp"
#include "ExecutionSpine.hpp"
#include "SPSCRingBuffer.hpp"
#include <thread>
#include <atomic>
#include <chrono>

namespace chimera {
namespace integration {

using namespace chimera::core;
using namespace chimera::spine;
using namespace chimera::infra;

// ==================== TRANSPORT ADAPTER ====================

class TransportAdapter {
public:
    TransportAdapter(UnifiedEngineCoordinator& coordinator, BinaryJournal& journal)
        : m_coordinator(coordinator)
        , m_journal(journal)
    {
    }

    void on_fix_market_data(
        const std::string& symbol,
        double bid,
        double ask,
        uint64_t timestamp_ns)
    {
        MetalSymbol metal_symbol = parse_symbol(symbol);
        if (metal_symbol == MetalSymbol::XAUUSD || metal_symbol == MetalSymbol::XAGUSD)
        {
            const double mid = (bid + ask) / 2.0;
            const double spread = ask - bid;
            const double ofi = calculate_ofi(bid, ask); // Placeholder

            MarketTickEvent tick{
                metal_symbol,
                bid,
                ask,
                mid,
                ofi,
                spread,
                timestamp_ns
            };

            m_coordinator.on_market_tick(tick);

            // Journal tick event
            TickEventPayload payload{
                metal_symbol,
                {0},
                bid,
                ask,
                mid,
                ofi,
                spread
            };
            m_journal.write_event(EventType::TICK, payload, timestamp_ns);
        }
    }

    void on_fix_execution_report(
        const std::string& symbol,
        const std::string& side,
        double quantity,
        double fill_price,
        bool is_close,
        uint64_t timestamp_ns)
    {
        MetalSymbol metal_symbol = parse_symbol(symbol);
        TradeSide trade_side = parse_side(side);

        ExecutionEvent execution{
            metal_symbol,
            trade_side,
            quantity,
            fill_price,
            timestamp_ns - 2000000,  // Estimate send time
            timestamp_ns - 1000000,  // Estimate ack time
            timestamp_ns,
            is_close,
            true
        };

        m_coordinator.on_execution(execution);

        // Journal execution event
        ExecutionEventPayload payload{
            metal_symbol,
            trade_side,
            static_cast<uint8_t>(is_close ? 1 : 0),
            1,
            {0},
            quantity,
            fill_price,
            execution.send_timestamp_ns,
            execution.ack_timestamp_ns,
            execution.fill_timestamp_ns
        };
        m_journal.write_event(EventType::EXECUTION, payload, timestamp_ns);
    }

private:
    UnifiedEngineCoordinator& m_coordinator;
    BinaryJournal& m_journal;

    MetalSymbol parse_symbol(const std::string& symbol)
    {
        if (symbol == "XAUUSD")
            return MetalSymbol::XAUUSD;
        else if (symbol == "XAGUSD")
            return MetalSymbol::XAGUSD;
        return MetalSymbol::XAUUSD; // Default
    }

    TradeSide parse_side(const std::string& side)
    {
        if (side == "BUY" || side == "1")
            return TradeSide::BUY;
        else if (side == "SELL" || side == "2")
            return TradeSide::SELL;
        return TradeSide::NONE;
    }

    double calculate_ofi(double bid, double ask)
    {
        // Placeholder - implement actual OFI calculation from order book
        return 0.0;
    }
};

// ==================== TELEMETRY PUBLISHER ====================

class TelemetryPublisher {
public:
    TelemetryPublisher(UnifiedEngineCoordinator& coordinator)
        : m_coordinator(coordinator)
        , m_running(false)
    {
    }

    void start()
    {
        m_running = true;
        m_thread = std::thread(&TelemetryPublisher::run, this);
    }

    void stop()
    {
        m_running = false;
        if (m_thread.joinable())
            m_thread.join();
    }

private:
    void run()
    {
        while (m_running)
        {
            auto snapshot = m_coordinator.get_telemetry_snapshot();
            
            // Publish to WebSocket, stdout, or other sinks
            publish_snapshot(snapshot);

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    void publish_snapshot(const TelemetrySnapshot& snapshot)
    {
        // Implementation would send to WebSocket server or stdout
        // For now, just a placeholder
    }

    UnifiedEngineCoordinator& m_coordinator;
    std::atomic<bool> m_running;
    std::thread m_thread;
};

// ==================== COMPLETE SYSTEM ====================

class ChimeraSystem {
public:
    ChimeraSystem()
        : m_journal("chimera_journal.bin")
        , m_coordinator()
        , m_transport_adapter(m_coordinator, m_journal)
        , m_telemetry_publisher(m_coordinator)
    {
    }

    void start()
    {
        m_telemetry_publisher.start();
    }

    void stop()
    {
        m_telemetry_publisher.stop();
        m_journal.flush();
    }

    // Market data interface
    void process_market_tick(
        const std::string& symbol,
        double bid,
        double ask,
        uint64_t timestamp_ns)
    {
        m_transport_adapter.on_fix_market_data(symbol, bid, ask, timestamp_ns);
    }

    // Execution report interface
    void process_execution(
        const std::string& symbol,
        const std::string& side,
        double quantity,
        double fill_price,
        bool is_close,
        uint64_t timestamp_ns)
    {
        m_transport_adapter.on_fix_execution_report(
            symbol, side, quantity, fill_price, is_close, timestamp_ns);
    }

    // Process engine intents and get approved orders
    std::optional<AllocatedIntent> process_engine_cycle()
    {
        HFTEngineIntent hft_intent; // Placeholder - wire your HFT engine here
        return m_coordinator.process_intents(hft_intent);
    }

    // Update risk metrics
    void update_risk_state(
        double equity,
        double daily_pnl,
        double unrealized_pnl,
        int consecutive_losses,
        double volatility_score)
    {
        GlobalRiskMetrics metrics{
            equity,
            daily_pnl,
            unrealized_pnl,
            consecutive_losses,
            volatility_score,
            0
        };
        m_coordinator.update_risk_metrics(metrics);
    }

    // Query system state
    bool is_trading_halted() const
    {
        return m_coordinator.is_trading_halted();
    }

    double get_risk_scale() const
    {
        return m_coordinator.get_risk_scale();
    }

    TelemetrySnapshot get_telemetry() const
    {
        return m_coordinator.get_telemetry_snapshot();
    }

private:
    BinaryJournal m_journal;
    UnifiedEngineCoordinator m_coordinator;
    TransportAdapter m_transport_adapter;
    TelemetryPublisher m_telemetry_publisher;
};

} // namespace integration
} // namespace chimera
