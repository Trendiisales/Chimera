#include "shadow/WatchdogThread.hpp"
#include "shadow/CrashHandler.hpp"
#include <cstdio>
#include <ctime>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstring>

namespace shadow {

std::atomic<uint64_t> WatchdogThread::last_heartbeat_ms_{0};
std::atomic<bool> WatchdogThread::running_{false};
std::thread WatchdogThread::thread_;

static uint64_t now_ms() {
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return uint64_t(ts.tv_sec) * 1000ULL + ts.tv_nsec / 1000000ULL;
}

void WatchdogThread::start() {
    if (running_.load()) {
        printf("[WATCHDOG] Already running\n");
        return;
    }
    
    // Create log directory
    mkdir("/var/log/chimera", 0755);
    
    // Initialize heartbeat
    last_heartbeat_ms_ = now_ms();
    running_ = true;
    
    // Start thread
    thread_ = std::thread(threadFunc);
    
    printf("[WATCHDOG] Started (hang threshold: %lu ms)\n", HANG_THRESHOLD_MS);
}

void WatchdogThread::stop() {
    if (!running_.load()) {
        return;
    }
    
    printf("[WATCHDOG] Stopping...\n");
    running_ = false;
    
    if (thread_.joinable()) {
        thread_.join();
    }
    
    printf("[WATCHDOG] Stopped\n");
}

void WatchdogThread::heartbeat() {
    last_heartbeat_ms_ = now_ms();
}

bool WatchdogThread::isRunning() {
    return running_.load();
}

void WatchdogThread::threadFunc() {
    printf("[WATCHDOG] Thread started\n");
    
    while (running_.load()) {
        uint64_t now = now_ms();
        uint64_t last_hb = last_heartbeat_ms_.load();
        uint64_t elapsed = now - last_hb;
        
        // Write heartbeat log
        writeHeartbeat();
        
        // Check for hang
        if (elapsed > HANG_THRESHOLD_MS) {
            printf("[WATCHDOG] !!! HANG DETECTED !!! (elapsed: %lu ms)\n", elapsed);
            handleHang();
        }
        
        // Sleep 1 second
        sleep(1);
    }
    
    printf("[WATCHDOG] Thread exiting\n");
}

void WatchdogThread::writeHeartbeat() {
    int fd = open(HEARTBEAT_PATH, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) return;
    
    uint64_t now = now_ms();
    uint64_t last_hb = last_heartbeat_ms_.load();
    uint64_t elapsed = now - last_hb;
    
    time_t t = time(nullptr);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&t));
    
    dprintf(fd, "CHIMERA HEARTBEAT\n");
    dprintf(fd, "================================================================================\n");
    dprintf(fd, "Time:           %s\n", timebuf);
    dprintf(fd, "Last Heartbeat: %lu ms ago\n", elapsed);
    dprintf(fd, "Status:         %s\n", elapsed < HANG_THRESHOLD_MS ? "OK" : "HUNG");
    dprintf(fd, "================================================================================\n");
    
    fsync(fd);
    close(fd);
}

void WatchdogThread::handleHang() {
    // Dump backtrace
    CrashHandler::dumpBacktrace("WATCHDOG HANG DETECTED");
    
    // Log to console
    printf("[WATCHDOG] Emergency backtrace written\n");
    printf("[WATCHDOG] System appears to be hung\n");
    printf("[WATCHDOG] Check /var/log/chimera/backtrace.log for details\n");
    
    // Don't kill the process - let it continue in case it recovers
    // But we've captured the state for debugging
}

} // namespace shadow
