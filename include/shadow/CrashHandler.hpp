#pragma once

#include <cstdint>
#include <string>

namespace shadow {

/**
 * CrashHandler - Install signal handlers for production safety
 * 
 * Features:
 * - Catches SIGSEGV, SIGABRT, SIGTERM, SIGINT
 * - Dumps backtrace to /var/log/chimera/backtrace.log
 * - Flushes all open positions to ledger
 * - Syncs journal before exit
 * 
 * Usage:
 *   CrashHandler::install();
 */
class CrashHandler {
public:
    // Install all signal handlers
    static void install();
    
    // Manually dump backtrace (for testing)
    static void dumpBacktrace(const std::string& reason);
    
    // Register a flush callback (called before exit)
    using FlushCallback = void(*)();
    static void registerFlushCallback(FlushCallback cb);
    
private:
    static void signalHandler(int sig);
    static void writeBacktrace(int fd, int sig);
    
    static constexpr const char* BACKTRACE_PATH = "/var/log/chimera/backtrace.log";
    static FlushCallback flush_callback_;
};

} // namespace shadow
