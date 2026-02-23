/**
 * Chimera Enhanced Trading System
 * 
 * This example shows how to integrate the new Metal Structure Engine,
 * Capital Allocator, Risk Governor, and Telemetry components with
 * the existing FIX baseline.
 * 
 * Build instructions:
 *   1. Add all new .hpp files to your include path
 *   2. Link with existing baseline libraries
 *   3. Compile with C++17: g++ -std=c++17 -O3 -pthread
 */

#include "ChimeraSystem.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <cmath>

namespace {

std::atomic<bool> g_running{true};

uint64_t get_timestamp_ns()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

} // anonymous namespace

// ==================== FIX INTEGRATION POINT ====================

/**
 * This function would be called from your existing FIX message handler
 * when you receive a Market Data Snapshot message (35=W)
 */
void on_fix_market_data_snapshot(
    chimera::integration::ChimeraSystem& system,
    const std::string& symbol,
    double bid,
    double ask)
{
    const uint64_t timestamp_ns = get_timestamp_ns();
    system.process_market_tick(symbol, bid, ask, timestamp_ns);
}

/**
 * This function would be called from your existing FIX message handler
 * when you receive an Execution Report (35=8)
 */
void on_fix_execution_report(
    chimera::integration::ChimeraSystem& system,
    const std::string& symbol,
    const std::string& side,
    double quantity,
    double fill_price,
    bool is_close)
{
    const uint64_t timestamp_ns = get_timestamp_ns();
    system.process_execution(symbol, side, quantity, fill_price, is_close, timestamp_ns);
}

// ==================== TRADING LOOP ====================

void trading_loop(chimera::integration::ChimeraSystem& system)
{
    std::cout << "Chimera trading loop started\n";

    // Example risk state
    double equity = 10000.0;
    double daily_pnl = 0.0;
    int consecutive_losses = 0;

    while (g_running)
    {
        // Update risk metrics every cycle
        system.update_risk_state(
            equity,
            daily_pnl,
            0.0, // unrealized PnL
            consecutive_losses,
            1.0  // volatility score
        );

        // Process engine intents
        auto approved_order = system.process_engine_cycle();
        
        if (approved_order)
        {
            // Send order to FIX transport
            std::cout << "Approved order: "
                      << "Symbol=" << static_cast<int>(approved_order->symbol)
                      << " Side=" << static_cast<int>(approved_order->side)
                      << " Qty=" << approved_order->quantity
                      << " Exit=" << approved_order->is_exit
                      << "\n";

            // Here you would call your existing FIX order submission function
            // send_fix_order(*approved_order);
        }

        // Check trading halt
        if (system.is_trading_halted())
        {
            std::cout << "TRADING HALTED - Daily drawdown limit reached\n";
        }

        // Print risk scale
        const double risk_scale = system.get_risk_scale();
        if (risk_scale < 1.0)
        {
            std::cout << "Risk scale reduced to: " << (risk_scale * 100.0) << "%\n";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "Chimera trading loop stopped\n";
}

// ==================== MAIN ====================

int main(int argc, char* argv[])
{
    std::cout << "========================================\n";
    std::cout << "Chimera Enhanced Trading System\n";
    std::cout << "========================================\n\n";

    try
    {
        // Initialize Chimera system
        chimera::integration::ChimeraSystem system;
        system.start();

        std::cout << "Chimera system initialized\n";

        // Start trading loop in separate thread
        std::thread trading_thread(trading_loop, std::ref(system));

        // ==================== SIMULATED MARKET DATA ====================
        // In production, this would come from your FIX session
        
        std::cout << "Simulating market data feed...\n\n";

        for (int i = 0; i < 100 && g_running; ++i)
        {
            // Simulate XAU tick
            const double xau_base = 2345.00;
            const double xau_noise = (std::sin(i * 0.1) * 2.0);
            const double xau_bid = xau_base + xau_noise;
            const double xau_ask = xau_bid + 0.50;

            on_fix_market_data_snapshot(system, "XAUUSD", xau_bid, xau_ask);

            // Simulate XAG tick
            const double xag_base = 28.50;
            const double xag_noise = (std::sin(i * 0.15) * 0.05);
            const double xag_bid = xag_base + xag_noise;
            const double xag_ask = xag_bid + 0.02;

            on_fix_market_data_snapshot(system, "XAGUSD", xag_bid, xag_ask);

            // Occasionally simulate an execution
            if (i % 20 == 0 && i > 0)
            {
                std::cout << "Simulating execution...\n";
                on_fix_execution_report(
                    system,
                    "XAUUSD",
                    "BUY",
                    1.0,
                    xau_bid,
                    false
                );
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "\nSimulation complete. Press Ctrl+C to exit.\n";

        // Wait for Ctrl+C
        trading_thread.join();

        // Shutdown
        system.stop();

        std::cout << "\nChimera system shutdown complete\n";
        std::cout << "========================================\n";

        // Print final telemetry
        auto telemetry = system.get_telemetry();
        std::cout << "Final Statistics:\n";
        std::cout << "  Total Trades: " << telemetry.total_trades << "\n";
        std::cout << "  Total PnL: $" << telemetry.total_pnl << "\n";
        std::cout << "  Max Drawdown: $" << telemetry.total_drawdown << "\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

// ==================== INTEGRATION NOTES ====================

/*

INTEGRATION WITH YOUR BASELINE:

1. FIX MESSAGE HANDLERS
   Replace the simulated market data with your actual FIX handlers:

   In your existing quote_session() function, when you parse:
   - Market Data Snapshot (35=W): Call on_fix_market_data_snapshot()
   - Execution Report (35=8): Call on_fix_execution_report()

2. ORDER SUBMISSION
   When process_engine_cycle() returns an AllocatedIntent:
   - Convert to FIX New Order Single (35=D)
   - Send through your existing SSL connection
   - Track the order for execution report matching

3. RISK INTEGRATION
   Connect to your existing risk modules:
   - Use BASELINE_20260223_035615/risk/CapitalAllocator for equity tracking
   - Feed consecutive losses from profit_controls/LossShutdownEngine
   - Use latency/LatencyAttributionEngine for latency metrics

4. TELEMETRY INTEGRATION
   Wire to your existing telemetry:
   - Use telemetry/TelemetryBus to broadcast events
   - Send to telemetry/TelemetryWsServer for GUI
   - Log to replay/ReplayRecorder for post-trade analysis

5. THREADING MODEL
   Recommended CPU core allocation:
   - Core 0: FIX market data ingest
   - Core 1: Chimera coordinator + engines
   - Core 2: FIX order transmission
   - Core 3: Telemetry publishing
   - Core 4: GUI WebSocket server

6. BUILD INTEGRATION
   Add to your CMakeLists.txt:

   add_subdirectory(chimera_extensions)
   
   target_link_libraries(chimera_main
       chimera_baseline
       chimera_engines
       chimera_allocation
       chimera_risk
       chimera_telemetry
       chimera_spine
   )

7. CONFIGURATION
   Extend your config.ini:

   [metal_structure]
   xau_max_exposure = 5.0
   xag_max_exposure = 3.0
   trend_threshold = 0.65
   ofi_threshold = 0.60

   [risk_governor]
   daily_drawdown_limit = 500.0
   max_consecutive_losses = 4
   volatility_kill_threshold = 2.0

DIRECTORY STRUCTURE:

Chimera/
├── BASELINE_20260223_035615/    (existing)
│   ├── risk/
│   ├── telemetry/
│   ├── replay/
│   └── src/
└── chimera_extensions/           (new)
    ├── engines/
    │   └── MetalStructureEngine.hpp
    ├── allocation/
    │   └── EnhancedCapitalAllocator.hpp
    ├── risk/
    │   └── RiskGovernor.hpp
    ├── telemetry/
    │   └── TelemetryCollector.hpp
    ├── spine/
    │   └── ExecutionSpine.hpp
    ├── infra/
    │   └── SPSCRingBuffer.hpp
    ├── core/
    │   └── UnifiedEngineCoordinator.hpp
    └── integration/
        └── ChimeraSystem.hpp

*/
