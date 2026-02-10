#pragma once

#include <cstdint>
#include <atomic>
#include <thread>
#include <string>

namespace shadow {

/**
 * WatchdogThread - Monitors system health and detects hangs
 * 
 * Features:
 * - Monitors global heartbeat timestamp
 * - Detects hangs (no heartbeat for >5 seconds)
 * - Writes heartbeat.log every second
 * - Triggers emergency flush + backtrace on hang
 * 
 * Usage:
 *   WatchdogThread::start();
 *   WatchdogThread::heartbeat();  // Call frequently
 *   WatchdogThread::stop();
 */
class WatchdogThread {
public:
    // Start watchdog thread
    static void start();
    
    // Stop watchdog thread
    static void stop();
    
    // Update heartbeat (call this frequently in main loop)
    static void heartbeat();
    
    // Check if watchdog is running
    static bool isRunning();
    
private:
    static void threadFunc();
    static void writeHeartbeat();
    static void handleHang();
    
    static constexpr const char* HEARTBEAT_PATH = "/var/log/chimera/heartbeat.log";
    static constexpr uint64_t HANG_THRESHOLD_MS = 5000;  // 5 seconds
    
    static std::atomic<uint64_t> last_heartbeat_ms_;
    static std::atomic<bool> running_;
    static std::thread thread_;
};

} // namespace shadow
