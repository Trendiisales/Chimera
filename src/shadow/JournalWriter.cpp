#include "shadow/JournalWriter.hpp"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <ctime>

namespace shadow {

int JournalWriter::fd_ = -1;
uint64_t JournalWriter::clordid_seq_ = CLORDID_START;
uint64_t JournalWriter::execid_seq_ = EXECID_START;
std::mutex JournalWriter::mutex_;

static uint64_t now_ms() {
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return uint64_t(ts.tv_sec) * 1000ULL + ts.tv_nsec / 1000000ULL;
}

void JournalWriter::init() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Create log directory
    mkdir("/var/log/chimera", 0755);
    
    // Open journal file (O_SYNC for crash-safety)
    fd_ = open(JOURNAL_PATH, O_CREAT | O_APPEND | O_WRONLY | O_SYNC, 0644);
    if (fd_ < 0) {
        perror("JournalWriter: open");
        exit(1);
    }
    
    // Write header
    char header[256];
    time_t now = time(nullptr);
    struct tm* tm = localtime(&now);
    snprintf(header, sizeof(header),
        "# CHIMERA FIX JOURNAL - Started %04d-%02d-%02d %02d:%02d:%02d\n",
        tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
        tm->tm_hour, tm->tm_min, tm->tm_sec
    );
    ssize_t written = write(fd_, header, strlen(header));
    if (written < 0) {
        perror("JournalWriter: write header");
    }
    
    printf("[JOURNAL] Initialized: %s\n", JOURNAL_PATH);
    printf("[JOURNAL] ClOrdID starts at: %lu\n", clordid_seq_);
    printf("[JOURNAL] ExecID starts at: %lu\n", execid_seq_);
}

uint64_t JournalWriter::logOrder(
    const std::string& symbol,
    const std::string& side,
    double qty,
    double price,
    const std::string& type
) {
    std::lock_guard<std::mutex> lock(mutex_);
    ensureOpen();
    
    uint64_t clid = clordid_seq_++;
    uint64_t ts = now_ms();
    
    // Format: D|<ClOrdID>|<Symbol>|<Side>|<Qty>|<Price>|<Type>|<Timestamp>
    char buf[512];
    snprintf(buf, sizeof(buf),
        "D|%lu|%s|%s|%.2f|%.2f|%s|%lu\n",
        clid,
        symbol.c_str(),
        side.c_str(),
        qty,
        price,
        type.c_str(),
        ts
    );
    
    writeEntry(buf);
    return clid;
}

void JournalWriter::logFill(
    uint64_t exec_id,
    uint64_t clord_id,
    double qty,
    double price,
    double fee
) {
    std::lock_guard<std::mutex> lock(mutex_);
    ensureOpen();
    
    uint64_t ts = now_ms();
    
    // Format: F|<ExecID>|<ClOrdID>|<Qty>|<Price>|<Fee>|<Timestamp>
    char buf[512];
    snprintf(buf, sizeof(buf),
        "F|%lu|%lu|%.2f|%.2f|%.4f|%lu\n",
        exec_id,
        clord_id,
        qty,
        price,
        fee,
        ts
    );
    
    writeEntry(buf);
}

uint64_t JournalWriter::nextClOrdID() {
    std::lock_guard<std::mutex> lock(mutex_);
    return clordid_seq_++;
}

uint64_t JournalWriter::nextExecID() {
    std::lock_guard<std::mutex> lock(mutex_);
    return execid_seq_++;
}

void JournalWriter::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fd_ >= 0) {
        fsync(fd_);
    }
}

void JournalWriter::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fd_ >= 0) {
        fsync(fd_);
        ::close(fd_);
        fd_ = -1;
        printf("[JOURNAL] Closed\n");
    }
}

void JournalWriter::ensureOpen() {
    if (fd_ < 0) {
        fprintf(stderr, "[JOURNAL] ERROR: Not initialized! Call init() first.\n");
        exit(1);
    }
}

void JournalWriter::writeEntry(const std::string& entry) {
    // O_SYNC flag means this will be written to disk immediately
    ssize_t n = write(fd_, entry.c_str(), entry.size());
    if (n != (ssize_t)entry.size()) {
        perror("[JOURNAL] write");
    }
}

} // namespace shadow
