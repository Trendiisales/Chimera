#pragma once

#include <queue>
#include <mutex>
#include <cstdint>

struct Intent {
    enum Side { BUY, SELL };
    Side side;
    const char* symbol;
    double qty;
    std::uint64_t ts_ns;

    Intent(Side s, const char* sym, double q, std::uint64_t ts)
    : side(s), symbol(sym), qty(q), ts_ns(ts) {}
};

class IntentQueue {
public:
    void push(const Intent& intent);
    bool try_pop(Intent& out);
private:
    std::queue<Intent> q_;
    std::mutex m_;
};
