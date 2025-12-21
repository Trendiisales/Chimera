#pragma once

#include <queue>
#include <mutex>

class Intent {
public:
    enum Side { BUY, SELL };

    Intent(Side s, const char* sym, double q)
    : side(s), qty(q) {}

    Side side;
    double qty;
};

class IntentQueue {
public:
    void push(const Intent& intent);
    bool try_pop(Intent& out);

private:
    std::queue<Intent> q_;
    std::mutex m_;
};
