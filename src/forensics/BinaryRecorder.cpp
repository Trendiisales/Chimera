#include "forensics/BinaryRecorder.hpp"
#include "forensics/CRC32.hpp"
#include <chrono>
#include <cstring>
#include <iostream>

using namespace chimera;

static uint64_t now_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

BinaryRecorder::BinaryRecorder(const std::string& path)
    : path_(path) {
    // FIX 1.3: ios::app — append to existing log instead of truncating.
    // Previously: ios::binary | ios::out — wiped the entire event log on every restart.
    // Snapshot restores causal ID continuity, but the log data was gone.
    // ReplayValidator had nothing to validate after a restart.
    // Now: log persists across restarts. Rotation handled internally at 1 GiB.
    file_.open(path_, std::ios::binary | std::ios::app);
}

// ---------------------------------------------------------------------------
// Rotate events.bin → events.<epoch_ms>.bin when size exceeds 1 GiB.
// Must be called under mtx_.
// ---------------------------------------------------------------------------
void BinaryRecorder::rotate_if_needed() {
    if (!file_.is_open()) return;

    // tellp() gives current write position = effective file size (append mode)
    auto pos = file_.tellp();
    if (pos < 0 || static_cast<uint64_t>(pos) < MAX_LOG_BYTES) return;

    file_.close();

    // Rename to timestamped archive
    using namespace std::chrono;
    auto epoch_ms = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()
    ).count();

    // Insert timestamp before .bin: /var/log/chimera/events.bin
    //                             → /var/log/chimera/events.1706000000000.bin
    std::string archived = path_;
    auto dot = archived.rfind(".bin");
    if (dot != std::string::npos) {
        archived.insert(dot, "." + std::to_string(epoch_ms));
    } else {
        archived += "." + std::to_string(epoch_ms);
    }
    std::rename(path_.c_str(), archived.c_str());

    std::cout << "[RECORDER] Rotated " << path_ << " -> " << archived << "\n";

    // Reopen fresh
    file_.open(path_, std::ios::binary | std::ios::app);
}

uint64_t BinaryRecorder::next_causal_id() {
    return causal_.fetch_add(1);
}

// FIX 3.2: Direct set — single store() instead of burn loop.
// ContextSnapshotter::load() calls this to restore causal continuity.
// Previously: while (next_causal_id() < saved) {} — spins millions of fetch_add
// on startup if saved_causal was large. At ~5-10ns per fetch_add, millions of
// events = tens of milliseconds of wasted startup time.
void BinaryRecorder::set_causal(uint64_t val) {
    causal_.store(val);
}

void BinaryRecorder::write(EventType type, const void* payload,
                            uint32_t size, uint64_t causal_id) {
    std::lock_guard<std::mutex> lock(mtx_);

    // Rotate before writing if we've hit the size cap
    rotate_if_needed();

    EventHeader hdr;
    hdr.ts_ns     = now_ns();
    hdr.causal_id = causal_id;
    hdr.type      = type;
    hdr.size      = size;
    hdr.crc       = CRC32::compute(payload, size);

    file_.write(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    file_.write(reinterpret_cast<const char*>(payload), size);
    file_.flush();
}

// ---------------------------------------------------------------------------
// Typed helpers — each constructs its own payload struct inline and delegates
// to the core write(). Causal ID is auto-allocated.
// ---------------------------------------------------------------------------

void BinaryRecorder::write_market(const char* symbol, double bid, double bid_qty,
                                   double ask, double ask_qty) {
    struct MarketEvent {
        char   symbol[16];
        double bid;
        double bid_qty;
        double ask;
        double ask_qty;
    };
    static_assert(sizeof(MarketEvent) == 48, "MarketEvent layout");

    MarketEvent ev{};
    std::strncpy(ev.symbol, symbol, sizeof(ev.symbol) - 1);
    ev.bid     = bid;
    ev.bid_qty = bid_qty;
    ev.ask     = ask;
    ev.ask_qty = ask_qty;

    write(EventType::MARKET_TICK, &ev, sizeof(ev), next_causal_id());
}

void BinaryRecorder::write_ack(const std::string& client_id, const std::string& exch_id) {
    struct AckEvent {
        char client_id[32];
        char exch_id[32];
    };
    static_assert(sizeof(AckEvent) == 64, "AckEvent layout");

    AckEvent ev{};
    std::strncpy(ev.client_id, client_id.c_str(), sizeof(ev.client_id) - 1);
    std::strncpy(ev.exch_id,   exch_id.c_str(),   sizeof(ev.exch_id)   - 1);

    write(EventType::ACK, &ev, sizeof(ev), next_causal_id());
}

void BinaryRecorder::write_fill(const std::string& client_id, double qty, double price) {
    struct FillEvent {
        char   client_id[32];
        double qty;
        double price;
    };
    static_assert(sizeof(FillEvent) == 48, "FillEvent layout");

    FillEvent ev{};
    std::strncpy(ev.client_id, client_id.c_str(), sizeof(ev.client_id) - 1);
    ev.qty   = qty;
    ev.price = price;

    write(EventType::FILL, &ev, sizeof(ev), next_causal_id());
}

void BinaryRecorder::write_cancel(const std::string& client_id) {
    struct CancelEvent {
        char client_id[32];
    };
    static_assert(sizeof(CancelEvent) == 32, "CancelEvent layout");

    CancelEvent ev{};
    std::strncpy(ev.client_id, client_id.c_str(), sizeof(ev.client_id) - 1);

    write(EventType::CANCEL, &ev, sizeof(ev), next_causal_id());
}

void BinaryRecorder::write_reject(const std::string& client_id) {
    struct RejectEvent {
        char client_id[32];
    };
    static_assert(sizeof(RejectEvent) == 32, "RejectEvent layout");

    RejectEvent ev{};
    std::strncpy(ev.client_id, client_id.c_str(), sizeof(ev.client_id) - 1);

    write(EventType::REJECT, &ev, sizeof(ev), next_causal_id());
}
