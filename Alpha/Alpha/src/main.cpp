// ═══════════════════════════════════════════════════════════════════════════════
// Alpha Trading System - Main Entry Point
// ═══════════════════════════════════════════════════════════════════════════════
// VERSION: 1.0.0
//
// USAGE:
//   ./alpha                    # Shadow mode (default)
//   ./alpha --live             # Live trading
//   ./alpha --equity 50000     # Custom equity
//   ./alpha --help             # Show help
//
// REQUIREMENTS:
//   - config.ini with valid cTrader FIX credentials
//   - OpenSSL for FIX SSL connection
//   - Linux (WSL2) or Windows
// ═══════════════════════════════════════════════════════════════════════════════

#include <iostream>
#include <csignal>
#include <cstring>

#include "AlphaEngine.hpp"

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
        } else if (std::strcmp(argv[i], "--verbose") == 0) {
            config.verbose = true;
        } else if (std::strcmp(argv[i], "--equity") == 0 && i + 1 < argc) {
            config.initial_equity = std::stod(argv[++i]);
        } else if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config.config_path = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::cout << "\n"
                      << "╔════════════════════════════════════════════════════════════════╗\n"
                      << "║  Alpha Trading System v" << Alpha::VERSION << "                                ║\n"
                      << "╠════════════════════════════════════════════════════════════════╣\n"
                      << "║                                                                ║\n"
                      << "║  USAGE: alpha [OPTIONS]                                        ║\n"
                      << "║                                                                ║\n"
                      << "║  OPTIONS:                                                      ║\n"
                      << "║    --shadow        Shadow/paper trading (default)              ║\n"
                      << "║    --live          Live trading mode                           ║\n"
                      << "║    --equity N      Initial equity (default: 10000)             ║\n"
                      << "║    --config PATH   Config file path (default: config.ini)      ║\n"
                      << "║    --verbose       Verbose output                              ║\n"
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
// MAIN
// ═══════════════════════════════════════════════════════════════════════════════
int main(int argc, char* argv[]) {
    std::cout << "\n[ALPHA] Starting Alpha Trading System...\n";
    
    // Parse arguments
    auto config = parse_args(argc, argv);
    
    // Set up signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);  // Ignore broken pipe
#endif
    
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
