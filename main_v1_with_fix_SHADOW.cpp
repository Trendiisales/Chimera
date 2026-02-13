#include "shadow/MultiSymbolExecutor.hpp"
#include "core/Globals.hpp"
#include "gui/GUIBroadcaster.hpp"
#include "gui/WsServer.hpp"
#include "shadow/ShadowTypes.hpp"

// FIX Integration
#include "fix/FIXConfig.hpp"
#include "fix/CTraderFIXClient.hpp"

#include <iostream>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>

using namespace shadow;

extern WsServer* g_wsServer;

static std::atomic<bool> g_running{true};

void signal_handler(int signal) {
    std::cout << "\n[SIGNAL] Caught signal " << signal << ", shutting down...\n";
    g_running = false;
}

int main() {
    // Install signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "=============================================================\n";
    std::cout << "CHIMERA V1 - SHADOW MODE EXECUTION ENGINE\n";
    std::cout << "=============================================================\n";
    std::cout << "Mode: SHADOW (NO REAL ORDERS)\n";
    std::cout << "Market Data: LIVE (cTrader FIX)\n";
    std::cout << "Execution: SIMULATED\n";
    std::cout << "=============================================================\n\n";

    // Load FIX config
    FIXConfig fix_config;
    if (!fix_config.isValid()) {
        std::cerr << "[ERROR] Invalid FIX configuration\n";
        return 1;
    }

    std::cout << "[Config] FIX configuration loaded:\n";
    fix_config.print();
    std::cout << "\n";

    // Initialize execution engine
    std::cout << "[Engine] Initializing MultiSymbolExecutor...\n";
    static MultiSymbolExecutor executor;
    g_executor = &executor;

    // Add symbols in SHADOW mode
    std::cout << "[Engine] Registering symbols in SHADOW mode:\n";
    executor.addSymbol({ "XAUUSD" }, ExecMode::SHADOW);
    std::cout << "  ✓ XAUUSD - SHADOW\n";
    executor.addSymbol({ "XAGUSD" }, ExecMode::SHADOW);
    std::cout << "  ✓ XAGUSD - SHADOW\n";
    std::cout << "\n";

    // Start WebSocket server for GUI
    std::cout << "[GUI] Starting WebSocket server on port 7777...\n";
    static WsServer ws(7777);
    g_wsServer = &ws;
    ws.start();
    std::cout << "[GUI] WebSocket server running\n";
    std::cout << "[GUI] Dashboard: ws://185.167.119.59:7777\n\n";

    // Start GUI broadcaster
    std::cout << "[GUI] Starting GUIBroadcaster...\n";
    Chimera::GUIBroadcaster broadcaster;
    broadcaster.start();
    std::cout << "[GUI] Broadcasting execution state at 1 Hz\n\n";

    // Initialize FIX client
    std::cout << "[FIX] Initializing cTrader FIX client...\n";
    CTraderFIXClient fix_client;
    fix_client.setConfig(fix_config);

    // Set tick callback - feed to executor
    fix_client.setOnTick([&](const CTraderTick& ctick) {
        // Convert CTraderTick to shadow::Tick
        shadow::Tick tick;
        tick.bid = ctick.bid;
        tick.ask = ctick.ask;
        tick.ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();

        // Feed to executor
        executor.onTick(ctick.symbol, tick);
    });

    // Set latency callback (optional - for monitoring)
    fix_client.setOnLatency([&](const std::string& symbol, double rtt_ms, double slippage_bps) {
        // Just log it - executor handles latency internally
        static auto last_log = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_log).count() >= 10) {
            std::cout << "[LATENCY] " << symbol << " RTT: " << rtt_ms << " ms\n";
            last_log = now;
        }
    });

    // Set execution callback (shouldn't fire in SHADOW mode, but handle anyway)
    fix_client.setOnExec([&](const CTraderExecReport& report) {
        std::cout << "[WARNING] Execution report received in SHADOW mode: "
                  << report.symbol << " " << report.side 
                  << " " << report.orderQty << " @ " << report.avgPx << "\n";
    });

    // Connect to cTrader
    std::cout << "[FIX] Connecting to cTrader...\n";
    if (!fix_client.connect()) {
        std::cerr << "[FATAL] FIX connection failed\n";
        return 1;
    }

    std::cout << "[FIX] ✓ Connection established\n\n";

    // Wait for system ready
    std::cout << "[FIX] Waiting for system ready...\n";
    for (int i = 0; i < 50 && fix_client.getState() != CTraderState::RUNNING; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (fix_client.getState() == CTraderState::RUNNING) {
        std::cout << "[FIX] ✓ System ready - all sessions active\n\n";
    } else {
        std::cout << "[FIX] ⚠ System not fully ready but continuing...\n\n";
    }

    // Request security list
    std::cout << "[FIX] Requesting security list...\n";
    fix_client.requestSecurityList();

    for (int i = 0; i < 100 && !fix_client.isSecurityListReady(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (fix_client.isSecurityListReady()) {
        std::cout << "[FIX] ✓ Security list loaded (" 
                  << fix_client.getSecurityListCount() << " symbols)\n\n";
    } else {
        std::cout << "[FIX] ⚠ Security list timeout, continuing...\n\n";
    }

    // Subscribe to market data
    std::cout << "[FIX] Subscribing to market data...\n";
    fix_client.subscribeMarketData("XAUUSD");
    std::cout << "  ✓ XAUUSD subscription sent\n";
    fix_client.subscribeMarketData("XAGUSD");
    std::cout << "  ✓ XAGUSD subscription sent\n\n";

    std::cout << "=============================================================\n";
    std::cout << "CHIMERA V1 RUNNING - SHADOW MODE\n";
    std::cout << "=============================================================\n";
    std::cout << "Market Data: LIVE (cTrader FIX)\n";
    std::cout << "Execution: SHADOW (No real orders)\n";
    std::cout << "Dashboard: ws://185.167.119.59:7777\n";
    std::cout << "Press Ctrl+C to shutdown\n";
    std::cout << "=============================================================\n\n";

    // Status monitoring loop
    auto last_status = std::chrono::steady_clock::now();
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Print status every 30 seconds
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_status).count() >= 30) {
            std::cout << "\n[STATUS] "
                      << "Total PnL: $" << executor.getTotalRealizedPnl() 
                      << " | Active Legs: " << executor.getTotalActiveLegs()
                      << " | Flat: " << (executor.isFullyFlat() ? "YES" : "NO")
                      << "\n";
            last_status = now;
        }
    }

    std::cout << "\n[SHUTDOWN] Disconnecting...\n";
    fix_client.disconnect();
    std::cout << "[SHUTDOWN] Complete\n";

    return 0;
}
