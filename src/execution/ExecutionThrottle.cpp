#include "execution/ExecutionThrottle.hpp"
#include <chrono>

using namespace chimera;

ExecutionThrottle::ExecutionThrottle(uint32_t global_rate, uint32_t per_symbol_rate)
    : global_rate_(global_rate), per_symbol_rate_(per_symbol_rate) {}

uint64_t ExecutionThrottle::now_ns() const {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

bool ExecutionThrottle::allow_global() {
    std::lock_guard<std::mutex> lock(mtx_);
    uint64_t now = now_ns();
    if (now - global_window_ > 1'000'000'000ULL) {
        global_window_ = now;
        global_count_ = 0;
    }
    if (global_count_ >= global_rate_) return false;
    ++global_count_;
    return true;
}

bool ExecutionThrottle::allow_symbol(const std::string& sym) {
    std::lock_guard<std::mutex> lock(mtx_);
    uint64_t now = now_ns();
    uint64_t& win = sym_window_[sym];
    uint32_t& cnt = sym_count_[sym];
    if (now - win > 1'000'000'000ULL) { win = now; cnt = 0; }
    if (cnt >= per_symbol_rate_) return false;
    ++cnt;
    return true;
}
