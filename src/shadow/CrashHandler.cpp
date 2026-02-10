#include "shadow/CrashHandler.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <ctime>

namespace shadow {

CrashHandler::FlushCallback CrashHandler::flush_callback_ = nullptr;

void CrashHandler::install() {
    // Create log directory if it doesn't exist
    mkdir("/var/log/chimera", 0755);
    
    // Install signal handlers
    signal(SIGSEGV, signalHandler);  // Segfault
    signal(SIGABRT, signalHandler);  // Abort
    signal(SIGTERM, signalHandler);  // Terminate
    signal(SIGINT,  signalHandler);  // Ctrl+C
    signal(SIGFPE,  signalHandler);  // Floating point exception
    signal(SIGILL,  signalHandler);  // Illegal instruction
    
    printf("[CRASH_HANDLER] Installed signal handlers\n");
    printf("[CRASH_HANDLER] Backtrace will be written to: %s\n", BACKTRACE_PATH);
}

void CrashHandler::registerFlushCallback(FlushCallback cb) {
    flush_callback_ = cb;
    printf("[CRASH_HANDLER] Registered flush callback\n");
}

void CrashHandler::signalHandler(int sig) {
    // Call flush callback if registered
    if (flush_callback_) {
        flush_callback_();
    }
    
    // Open backtrace file
    int fd = open(BACKTRACE_PATH, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        _exit(128 + sig);
    }
    
    // Write signal info and backtrace
    writeBacktrace(fd, sig);
    
    // Sync and close
    fsync(fd);
    close(fd);
    
    // Exit with signal code
    _exit(128 + sig);
}

void CrashHandler::writeBacktrace(int fd, int sig) {
    // Get current time
    time_t now = time(nullptr);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    // Write header
    const char* sig_name = "UNKNOWN";
    switch(sig) {
        case SIGSEGV: sig_name = "SIGSEGV (Segmentation Fault)"; break;
        case SIGABRT: sig_name = "SIGABRT (Abort)"; break;
        case SIGTERM: sig_name = "SIGTERM (Terminate)"; break;
        case SIGINT:  sig_name = "SIGINT (Interrupt)"; break;
        case SIGFPE:  sig_name = "SIGFPE (Floating Point Exception)"; break;
        case SIGILL:  sig_name = "SIGILL (Illegal Instruction)"; break;
    }
    
    dprintf(fd, "================================================================================\n");
    dprintf(fd, "CHIMERA CRASH REPORT\n");
    dprintf(fd, "================================================================================\n");
    dprintf(fd, "Time:   %s\n", timebuf);
    dprintf(fd, "Signal: %d (%s)\n", sig, sig_name);
    dprintf(fd, "PID:    %d\n", getpid());
    dprintf(fd, "================================================================================\n\n");
    
    // Get backtrace
    void* bt[128];
    int n = backtrace(bt, 128);
    
    dprintf(fd, "BACKTRACE (%d frames):\n", n);
    dprintf(fd, "--------------------------------------------------------------------------------\n");
    
    // Write backtrace symbols
    backtrace_symbols_fd(bt, n, fd);
    
    dprintf(fd, "\n================================================================================\n");
    dprintf(fd, "END OF CRASH REPORT\n");
    dprintf(fd, "================================================================================\n");
}

void CrashHandler::dumpBacktrace(const std::string& reason) {
    int fd = open(BACKTRACE_PATH, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (fd < 0) return;
    
    time_t now = time(nullptr);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    dprintf(fd, "\n[%s] Manual backtrace: %s\n", timebuf, reason.c_str());
    
    void* bt[128];
    int n = backtrace(bt, 128);
    backtrace_symbols_fd(bt, n, fd);
    
    dprintf(fd, "\n");
    fsync(fd);
    close(fd);
}

} // namespace shadow
