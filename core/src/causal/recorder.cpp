#include "chimera/causal/recorder.hpp"
#include <cstring>
#include <iomanip>

namespace chimera::causal {

Recorder::Recorder(const std::string& base_path)
    : counter(1) {
    bin.open(base_path + ".bin", std::ios::binary | std::ios::out);
    jsonl.open(base_path + ".jsonl", std::ios::out);
    
    if (!bin.is_open()) {
        throw std::runtime_error("Failed to open binary log: " + base_path + ".bin");
    }
    if (!jsonl.is_open()) {
        throw std::runtime_error("Failed to open JSONL log: " + base_path + ".jsonl");
    }
}

Recorder::~Recorder() {
    flush();
    if (bin.is_open()) bin.close();
    if (jsonl.is_open()) jsonl.close();
}

event_id_t Recorder::next_id() {
    return counter.fetch_add(1, std::memory_order_relaxed);
}

void Recorder::flush() {
    std::lock_guard<std::mutex> lock(mtx);
    bin.flush();
    jsonl.flush();
}

template<typename T>
void Recorder::write(const T& e) {
    std::lock_guard<std::mutex> lock(mtx);
    
    // Binary write
    bin.write(reinterpret_cast<const char*>(&e), sizeof(T));
    
    // JSONL write
    jsonl << "{"
          << "\"id\":" << e.h.id << ","
          << "\"parent\":" << e.h.parent_id << ","
          << "\"type\":" << static_cast<int>(e.h.type) << ","
          << "\"ts_ns\":" << e.h.ts_ns << ","
          << "\"symbol\":" << e.h.symbol;
    
    // Type-specific fields
    if constexpr (std::is_same_v<T, TickEvent>) {
        jsonl << ",\"bid\":" << std::fixed << std::setprecision(8) << e.bid
              << ",\"ask\":" << e.ask
              << ",\"bid_sz\":" << e.bid_sz
              << ",\"ask_sz\":" << e.ask_sz;
    } else if constexpr (std::is_same_v<T, DecisionEvent>) {
        jsonl << ",\"engine_id\":" << e.engine_id
              << ",\"edge_score\":" << std::fixed << std::setprecision(6) << e.edge_score;
    } else if constexpr (std::is_same_v<T, RiskEvent>) {
        jsonl << ",\"allowed\":" << (e.allowed ? "true" : "false")
              << ",\"max_pos\":" << e.max_pos
              << ",\"cur_pos\":" << e.cur_pos;
    } else if constexpr (std::is_same_v<T, OrderIntentEvent>) {
        jsonl << ",\"is_buy\":" << (e.is_buy ? "true" : "false")
              << ",\"price\":" << std::fixed << std::setprecision(8) << e.price
              << ",\"qty\":" << e.qty;
    } else if constexpr (std::is_same_v<T, VenueAckEvent>) {
        jsonl << ",\"accepted\":" << (e.accepted ? "true" : "false")
              << ",\"venue_code\":" << e.venue_code;
    } else if constexpr (std::is_same_v<T, FillEvent>) {
        jsonl << ",\"fill_price\":" << std::fixed << std::setprecision(8) << e.fill_price
              << ",\"fill_qty\":" << e.fill_qty;
    } else if constexpr (std::is_same_v<T, PnLAttributionEvent>) {
        jsonl << ",\"pnl\":" << std::fixed << std::setprecision(8) << e.pnl
              << ",\"fee\":" << e.fee
              << ",\"engine_id\":" << e.engine_id;
    }
    
    jsonl << "}\n";
}

void Recorder::record(const TickEvent& e) { write(e); }
void Recorder::record(const DecisionEvent& e) { write(e); }
void Recorder::record(const RiskEvent& e) { write(e); }
void Recorder::record(const OrderIntentEvent& e) { write(e); }
void Recorder::record(const VenueAckEvent& e) { write(e); }
void Recorder::record(const FillEvent& e) { write(e); }
void Recorder::record(const PnLAttributionEvent& e) { write(e); }

}
