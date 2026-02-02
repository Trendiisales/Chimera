#include "runtime/ContextSnapshotter.hpp"
#include "runtime/ContextSnapshot.hpp"
#include "forensics/CRC32.hpp"
#include <fstream>
#include <vector>
#include <chrono>
#include <cstring>
#include <iostream>

using namespace chimera;

static uint64_t now_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

// --- Serialization helpers ---
template<typename T>
static void append_pod(std::vector<uint8_t>& buf, const T& val) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&val);
    buf.insert(buf.end(), p, p + sizeof(T));
}

static void append_string(std::vector<uint8_t>& buf, const std::string& s) {
    uint32_t len = static_cast<uint32_t>(s.size());
    append_pod(buf, len);
    buf.insert(buf.end(), s.begin(), s.end());
}

// --- Deserialization helpers ---
template<typename T>
static void read_pod(const uint8_t*& p, T& val) {
    std::memcpy(&val, p, sizeof(T));
    p += sizeof(T);
}

static void read_string(const uint8_t*& p, std::string& s) {
    uint32_t len;
    read_pod(p, len);
    s.assign(reinterpret_cast<const char*>(p), len);
    p += len;
}

// Serialize an OrderRecord into the buffer
static void append_order(std::vector<uint8_t>& buf, const OrderRecord& rec) {
    append_string(buf, rec.client_id);
    append_string(buf, rec.exchange_id);
    append_string(buf, rec.symbol);
    append_pod(buf, rec.price);
    append_pod(buf, rec.qty);
    uint16_t status = static_cast<uint16_t>(rec.status);
    append_pod(buf, status);
    append_pod(buf, rec.last_update_ns);
}

// Deserialize an OrderRecord from the buffer
static OrderRecord read_order(const uint8_t*& p) {
    OrderRecord rec;
    read_string(p, rec.client_id);
    read_string(p, rec.exchange_id);
    read_string(p, rec.symbol);
    read_pod(p, rec.price);
    read_pod(p, rec.qty);
    uint16_t status;
    read_pod(p, status);
    rec.status = static_cast<OrderStatus>(status);
    read_pod(p, rec.last_update_ns);
    return rec;
}

// ============================================================
ContextSnapshotter::ContextSnapshotter(Context& ctx) : ctx_(ctx) {}

bool ContextSnapshotter::save(const std::string& path) {
    std::vector<uint8_t> payload;

    // --- ARM STATE ---
    bool armed   = ctx_.arm.live_enabled();  // true only if armed+verified
    bool armed_only = (ctx_.arm.status() != "DISARMED"); // armed but maybe not verified
    append_pod(payload, armed_only);
    append_pod(payload, armed);

    // --- RISK POSITIONS ---
    auto positions = ctx_.risk.dump_positions();
    uint32_t pos_count = static_cast<uint32_t>(positions.size());
    append_pod(payload, pos_count);
    for (auto& kv : positions) {
        append_string(payload, kv.first);
        append_pod(payload, kv.second);   // double qty
    }

    // --- QUEUE BOOKS ---
    auto books = ctx_.queue.dump_books();
    uint32_t book_count = static_cast<uint32_t>(books.size());
    append_pod(payload, book_count);
    for (auto& kv : books) {
        append_string(payload, kv.first);
        append_pod(payload, kv.second);   // QueueState is POD — safe
    }

    // --- RECORDER CAUSAL COUNTER ---
    // Preserve causal chain continuity across restarts
    uint64_t next_causal = ctx_.recorder.next_causal_id();
    append_pod(payload, next_causal);

    // --- FIX 4.3: OPEN ORDERS ---
    // Persist all orders so in-flight order state survives crashes.
    // On restore, open orders (NEW/ACKED/PARTIALLY_FILLED) will be
    // reconciled against exchange truth in ColdStartReconciler.
    auto orders = ctx_.osm.dump_orders();
    uint32_t order_count = static_cast<uint32_t>(orders.size());
    append_pod(payload, order_count);
    for (auto& rec : orders) {
        append_order(payload, rec);
    }

    // --- HEADER + WRITE ---
    SnapshotHeader hdr;
    hdr.ts_ns  = now_ns();
    hdr.size   = static_cast<uint32_t>(payload.size());
    hdr.crc    = CRC32::compute(payload.data(), payload.size());

    std::ofstream file(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!file) {
        std::cout << "[SNAPSHOT] Cannot open " << path << "\n";
        return false;
    }

    file.write(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    file.write(reinterpret_cast<char*>(payload.data()), payload.size());

    std::cout << "[SNAPSHOT] Saved " << payload.size() << "B to " << path
              << " (orders=" << order_count << ")\n";
    return true;
}

bool ContextSnapshotter::load(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::in);
    if (!file) {
        std::cout << "[SNAPSHOT] No snapshot at " << path << " — clean start\n";
        return false;
    }

    SnapshotHeader hdr;
    file.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));

    if (hdr.magic != 0x43484D52) {
        std::cout << "[SNAPSHOT] Bad magic — corrupt file\n";
        return false;
    }

    std::vector<uint8_t> payload(hdr.size);
    file.read(reinterpret_cast<char*>(payload.data()), hdr.size);

    uint32_t crc = CRC32::compute(payload.data(), payload.size());
    if (crc != hdr.crc) {
        std::cout << "[SNAPSHOT] CRC mismatch — corrupt file\n";
        return false;
    }

    const uint8_t* p = payload.data();

    // --- ARM ---
    // B5 FIX: use restore() — not request_arm() which resets state.
    // verified is always forced false on cold start (must re-verify with exchange).
    bool armed_only, armed;
    read_pod(p, armed_only);
    read_pod(p, armed);
    ctx_.arm.restore(armed_only, armed);

    // --- RISK POSITIONS ---
    uint32_t pos_count;
    read_pod(p, pos_count);
    ctx_.risk.clear_positions();
    for (uint32_t i = 0; i < pos_count; ++i) {
        std::string sym;
        double qty;
        read_string(p, sym);
        read_pod(p, qty);
        ctx_.risk.restore_position(sym, qty);
    }

    // --- QUEUE BOOKS ---
    uint32_t book_count;
    read_pod(p, book_count);
    ctx_.queue.clear();
    for (uint32_t i = 0; i < book_count; ++i) {
        std::string sym;
        QueueState st;
        read_string(p, sym);
        read_pod(p, st);       // QueueState is POD — safe
        ctx_.queue.restore(sym, st);
    }

    // --- CAUSAL COUNTER ---
    // FIX 3.2: Direct set via set_causal() — O(1) instead of burn loop.
    // Previously: while (next_causal_id() < saved) {} burned through millions
    // of fetch_add on startup if saved_causal was large.
    uint64_t saved_causal;
    read_pod(p, saved_causal);
    ctx_.recorder.set_causal(saved_causal);

    // --- FIX 4.3: OPEN ORDERS ---
    uint32_t order_count;
    read_pod(p, order_count);
    for (uint32_t i = 0; i < order_count; ++i) {
        OrderRecord rec = read_order(p);
        ctx_.osm.restore_order(rec);
    }

    std::cout << "[SNAPSHOT] Loaded from " << path
              << " (positions=" << pos_count
              << " books=" << book_count
              << " orders=" << order_count
              << " causal=" << saved_causal << ")\n";
    return true;
}
