// ═══════════════════════════════════════════════════════════════════════════════
// Alpha Trading System - Main Entry Point v1.2.0
// ═══════════════════════════════════════════════════════════════════════════════
// VERSION: 1.2.0
//
// USAGE:
//   ./alpha                         # Shadow mode with GUI (default)
//   ./alpha --live                  # Live trading
//   ./alpha --no-gui                # Disable text GUI
//   ./alpha --no-ml                 # Disable ML logging
//   ./alpha --equity 50000          # Custom equity
//   ./alpha --log-dir /path/to/logs # Custom log directory
//   ./alpha --help                  # Show help
//
// REQUIREMENTS:
//   - config.ini with valid cTrader FIX credentials
//   - OpenSSL for FIX SSL connection
//   - Linux (WSL2) or Windows
//
// NEW IN v1.2.0:
//   - Internal GUI cockpit (text-based)
//   - ML feature extraction and logging
//   - Enhanced trade/signal/tick logging
// ═══════════════════════════════════════════════════════════════════════════════

#include <iostream>
#include <csignal>
#include <cstring>
#include <thread>
#include <chrono>

// Use the new v1.2.0 engine
#include "AlphaEngine_v1.2.0.hpp"

// Global engine pointer for signal handler
Alpha::AlphaEngine* g_engine = nullptr;

// ═══════════════════════════════════════════════════════════════════════════════
// SIGNAL HANDLER
// ═══════════════════════════════════════════════════════════════════════════════
void signal_handler(int signum) {
    std::cout << "\n[ALPHA] Received signal " << signum << ", shutting down...\n";
    
    Alpha::getKillSwitch().kill("SIGNAL");
    
    if (g_engine) {
        g_engine->stop();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// ARGUMENT PARSER
// ═══════════════════════════════════════════════════════════════════════════════
Alpha::AlphaConfig parse_args(int argc, char* argv[]) {
    Alpha::AlphaConfig config;
    
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--live") == 0) {
            config.shadow_mode = false;
        } else if (std::strcmp(argv[i], "--shadow") == 0) {
            config.shadow_mode = true;
        } else if (std::strcmp(argv[i], "--verbose") == 0 || std::strcmp(argv[i], "-v") == 0) {
            config.verbose = true;
        } else if (std::strcmp(argv[i], "--equity") == 0 && i + 1 < argc) {
            config.initial_equity = std::stod(argv[++i]);
        } else if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config.config_path = argv[++i];
        }
        // GUI options
        else if (std::strcmp(argv[i], "--no-gui") == 0) {
            config.gui_enabled = false;
        } else if (std::strcmp(argv[i], "--gui") == 0) {
            config.gui_enabled = true;
        } else if (std::strcmp(argv[i], "--gui-refresh") == 0 && i + 1 < argc) {
            config.gui_refresh_ms = std::stoi(argv[++i]);
        }
        // ML options
        else if (std::strcmp(argv[i], "--no-ml") == 0) {
            config.ml_enabled = false;
        } else if (std::strcmp(argv[i], "--ml") == 0) {
            config.ml_enabled = true;
        } else if (std::strcmp(argv[i], "--ml-sample-rate") == 0 && i + 1 < argc) {
            config.ml_sample_rate = std::stoi(argv[++i]);
        }
        // Logging options
        else if (std::strcmp(argv[i], "--log-dir") == 0 && i + 1 < argc) {
            config.log_dir = argv[++i];
        } else if (std::strcmp(argv[i], "--log-ticks") == 0) {
            config.log_ticks = true;
        } else if (std::strcmp(argv[i], "--no-log") == 0) {
            config.log_trades = false;
            config.log_signals = false;
            config.log_events = false;
        }
        // Dashboard options
        else if (std::strcmp(argv[i], "--no-dashboard") == 0) {
            config.dashboard_enabled = false;
        } else if (std::strcmp(argv[i], "--dashboard-port") == 0 && i + 1 < argc) {
            config.dashboard_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
        // Help
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::cout << "\n"
                      << "╔════════════════════════════════════════════════════════════════╗\n"
                      << "║  Alpha Trading System v" << Alpha::ENGINE_VERSION << " (" << Alpha::ENGINE_CODENAME << ")                       ║\n"
                      << "╠════════════════════════════════════════════════════════════════╣\n"
                      << "║                                                                ║\n"
                      << "║  USAGE: alpha [OPTIONS]                                        ║\n"
                      << "║                                                                ║\n"
                      << "║  TRADING OPTIONS:                                              ║\n"
                      << "║    --shadow        Shadow/paper trading (default)              ║\n"
                      << "║    --live          Live trading mode                           ║\n"
                      << "║    --equity N      Initial equity (default: 10000)             ║\n"
                      << "║    --config PATH   Config file path (default: config.ini)      ║\n"
                      << "║    --verbose, -v   Verbose output                              ║\n"
                      << "║                                                                ║\n"
                      << "║  GUI OPTIONS:                                                  ║\n"
                      << "║    --gui           Enable GUI (default)                        ║\n"
                      << "║    --no-gui        Disable GUI                                 ║\n"
                      << "║    --gui-refresh N GUI refresh rate in ms (default: 500)       ║\n"
                      << "║                                                                ║\n"
                      << "║  ML OPTIONS:                                                   ║\n"
                      << "║    --ml            Enable ML logging (default)                 ║\n"
                      << "║    --no-ml         Disable ML logging                          ║\n"
                      << "║    --ml-sample-rate N  Log every N ticks (default: 100)        ║\n"
                      << "║                                                                ║\n"
                      << "║  LOGGING OPTIONS:                                              ║\n"
                      << "║    --log-dir PATH  Directory for log files (default: .)        ║\n"
                      << "║    --log-ticks     Enable binary tick logging                  ║\n"
                      << "║    --no-log        Disable all logging                         ║\n"
                      << "║                                                                ║\n"
                      << "║  DASHBOARD OPTIONS:                                            ║\n"
                      << "║    --no-dashboard  Disable WebSocket dashboard                 ║\n"
                      << "║    --dashboard-port N  Dashboard port (default: 8080)          ║\n"
                      << "║                                                                ║\n"
                      << "║    --help, -h      Show this help                              ║\n"
                      << "║                                                                ║\n"
                      << "║  INSTRUMENTS: XAUUSD, NAS100 (hardcoded)                       ║\n"
                      << "║                                                                ║\n"
                      << "║  REQUIRES: config.ini with valid cTrader FIX credentials       ║\n"
                      << "║                                                                ║\n"
                      << "╚════════════════════════════════════════════════════════════════╝\n\n";
            std::exit(0);
        }
    }
    
    return config;
}

// ═══════════════════════════════════════════════════════════════════════════════
// KEYBOARD INPUT HANDLER (for GUI mode)
// ═══════════════════════════════════════════════════════════════════════════════
void keyboard_handler(Alpha::AlphaEngine& engine) {
    std::cout << "\n[ALPHA] Keyboard commands:\n"
              << "  q - Quit\n"
              << "  k - Kill switch\n"
              << "  m - Toggle shadow/live mode\n"
              << "  s - Print status\n"
              << "\n";
    
    while (engine.is_running() && Alpha::getKillSwitch().alive()) {
        // Non-blocking keyboard check would go here
        // For now, just sleep
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// MAIN
// ═══════════════════════════════════════════════════════════════════════════════
int main(int argc, char* argv[]) {
    std::cout << "\n[ALPHA] Starting Alpha Trading System v" << Alpha::ENGINE_VERSION << "...\n";
    
    // Parse arguments
    auto config = parse_args(argc, argv);
    
    // Set up signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);  // Ignore broken pipe
#endif
    
    // Print configuration summary
    std::cout << "[ALPHA] Configuration:\n"
              << "  Mode: " << (config.shadow_mode ? "SHADOW" : "LIVE") << "\n"
              << "  Equity: $" << config.initial_equity << "\n"
              << "  GUI: " << (config.gui_enabled ? "ENABLED" : "DISABLED") << "\n"
              << "  ML Logging: " << (config.ml_enabled ? "ENABLED" : "DISABLED") << "\n"
              << "  Dashboard: " << (config.dashboard_enabled ? "ENABLED" : "DISABLED") << "\n"
              << "  Log Directory: " << config.log_dir << "\n";
    
    // Create engine
    Alpha::AlphaEngine engine(config);
    g_engine = &engine;
    
    // Start engine
    if (!engine.start()) {
        std::cerr << "[ALPHA] FATAL: Failed to start engine!\n";
        return 1;
    }
    
    // Wait for shutdown
    while (engine.is_running() && Alpha::getKillSwitch().alive()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    // Clean up
    engine.stop();
    g_engine = nullptr;
    
    std::cout << "[ALPHA] Goodbye.\n";
    return 0;
}
