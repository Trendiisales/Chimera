#pragma once

#include <cstdint>
#include <string>
#include <mutex>

namespace shadow {

/**
 * JournalWriter - FIX-sequence-accurate trade journaling
 * 
 * Features:
 * - ClOrdID sequence (starts at 10000)
 * - ExecID sequence (starts at 50000)
 * - Deterministic replay
 * - O_SYNC writes (crash-safe)
 * 
 * Journal Format:
 *   D|<ClOrdID>|<Symbol>|<Side>|<Qty>|<Price>|<Type>|<Timestamp>
 *   F|<ExecID>|<ClOrdID>|<Qty>|<Price>|<Fee>|<Timestamp>
 * 
 * Usage:
 *   JournalWriter::init();
 *   uint64_t clid = JournalWriter::logOrder(symbol, side, qty, price, type);
 *   JournalWriter::logFill(exec_id, clid, qty, price, fee);
 */
class JournalWriter {
public:
    // Initialize journal (creates file, starts sequences)
    static void init();
    
    // Log an order (returns ClOrdID)
    static uint64_t logOrder(
        const std::string& symbol,
        const std::string& side,
        double qty,
        double price,
        const std::string& type
    );
    
    // Log a fill
    static void logFill(
        uint64_t exec_id,
        uint64_t clord_id,
        double qty,
        double price,
        double fee
    );
    
    // Get next ClOrdID (without logging)
    static uint64_t nextClOrdID();
    
    // Get next ExecID (without logging)
    static uint64_t nextExecID();
    
    // Flush journal to disk
    static void flush();
    
    // Close journal
    static void close();
    
private:
    static void ensureOpen();
    static void writeEntry(const std::string& entry);
    
    static constexpr const char* JOURNAL_PATH = "/var/log/chimera/shadow_fix_journal.log";
    static constexpr uint64_t CLORDID_START = 10000;
    static constexpr uint64_t EXECID_START = 50000;
    
    static int fd_;
    static uint64_t clordid_seq_;
    static uint64_t execid_seq_;
    static std::mutex mutex_;
};

} // namespace shadow
