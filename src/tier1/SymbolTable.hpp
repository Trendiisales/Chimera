#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <array>

namespace chimera {

static constexpr int MAX_SYMBOLS = 3;

enum SymbolID : int {
    SYM_BTC = 0,
    SYM_ETH = 1,
    SYM_SOL = 2
};

static const std::array<const char*, MAX_SYMBOLS> SYMBOL_NAMES = {
    "BTCUSDT",
    "ETHUSDT",
    "SOLUSDT"
};

inline int symbol_name_to_id(const std::string& name) {
    if (name == "BTCUSDT") return SYM_BTC;
    if (name == "ETHUSDT") return SYM_ETH;
    if (name == "SOLUSDT") return SYM_SOL;
    return -1;
}

struct alignas(64) SymbolState {
    std::atomic<int64_t> position_q6;
    std::atomic<int64_t> cap_q6;
    std::atomic<uint64_t> blocked_until_ts;
    std::atomic<uint64_t> last_log_ts;
    
    SymbolState() {
        position_q6.store(0, std::memory_order_relaxed);
        cap_q6.store(50000, std::memory_order_relaxed);
        blocked_until_ts.store(0, std::memory_order_relaxed);
        last_log_ts.store(0, std::memory_order_relaxed);
    }
    
    bool is_blocked(uint64_t now_ms) const {
        return now_ms < blocked_until_ts.load(std::memory_order_relaxed);
    }
    
    void block(uint64_t now_ms, uint64_t duration_ms) {
        blocked_until_ts.store(now_ms + duration_ms, std::memory_order_release);
    }
    
    bool position_gate_allow(int64_t delta_q6) const {
        int64_t pos = position_q6.load(std::memory_order_relaxed);
        int64_t cap = cap_q6.load(std::memory_order_relaxed);
        int64_t new_pos = pos + delta_q6;
        
        return (new_pos <= cap && new_pos >= -cap);
    }
    
    void apply_fill(int64_t delta_q6) {
        position_q6.fetch_add(delta_q6, std::memory_order_release);
    }
    
    void set_cap(double cap_qty) {
        int64_t cap_val = static_cast<int64_t>(cap_qty * 1'000'000);
        cap_q6.store(cap_val, std::memory_order_release);
    }
    
    void set_position(double pos_qty) {
        int64_t pos_val = static_cast<int64_t>(pos_qty * 1'000'000);
        position_q6.store(pos_val, std::memory_order_release);
    }
    
    double get_position() const {
        return position_q6.load(std::memory_order_relaxed) / 1'000'000.0;
    }
    
    double get_cap() const {
        return cap_q6.load(std::memory_order_relaxed) / 1'000'000.0;
    }
    
    bool should_log(uint64_t now_ms, uint64_t interval_ms = 1000) {
        uint64_t last = last_log_ts.load(std::memory_order_relaxed);
        if (now_ms - last < interval_ms) {
            return false;
        }
        
        return last_log_ts.compare_exchange_strong(
            last, now_ms, std::memory_order_release);
    }
};

} // namespace chimera
