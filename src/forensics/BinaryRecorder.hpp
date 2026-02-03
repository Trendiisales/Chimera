#pragma once
#include <fstream>
#include <atomic>
#include <string>
#include <mutex>
#include "forensics/EventTypes.hpp"

namespace chimera {

class BinaryRecorder {
public:
    explicit BinaryRecorder(const std::string& path);

    uint64_t next_causal_id();

    // FIX 3.2: Direct causal counter set — used by ContextSnapshotter::load()
    // to restore causal continuity without burning through IDs via fetch_add loop.
    void set_causal(uint64_t val);

    void write(EventType type, const void* payload,
               uint32_t size, uint64_t causal_id);

    // Typed helpers — each emits the correct EventType + payload struct
    void write_market(const char* symbol, double bid, double bid_qty,
                      double ask, double ask_qty);
    void write_ack(const std::string& client_id, const std::string& exch_id);
    void write_fill(const std::string& client_id, double qty, double price);
    void write_cancel(const std::string& client_id);
    void write_reject(const std::string& client_id);

private:
    std::string       path_;
    std::ofstream     file_;
    std::atomic<uint64_t> causal_{1};
    std::mutex mtx_;

    // ---------------------------------------------------------------------------
    // Log rotation: when events.bin exceeds MAX_LOG_BYTES (1 GiB), close it,
    // rename to events.<timestamp>.bin, and reopen a fresh file.
    // Called under mtx_ — no extra locking needed.
    // ---------------------------------------------------------------------------
    static constexpr uint64_t MAX_LOG_BYTES = 1ULL << 30;  // 1 GiB
    void rotate_if_needed();
};

}
