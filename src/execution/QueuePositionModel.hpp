#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <cstdint>

namespace chimera {

struct QueueState {
    double bid_price{0.0};
    double ask_price{0.0};
    double bid_depth{0.0};
    double ask_depth{0.0};
    uint64_t last_update_ns{0};
};

// ---------------------------------------------------------------------------
// Top-of-book snapshot for strategy consumption.
// valid=false if symbol has never received a book update.
// ---------------------------------------------------------------------------
struct TopOfBook {
    double bid{0.0};
    double ask{0.0};
    double bid_size{0.0};
    double ask_size{0.0};
    uint64_t ts_ns{0};
    bool valid{false};
};

struct OrderQueueEstimate {
    double ahead_qty{0.0};
    double behind_qty{0.0};
    double expected_fill_prob{0.0};
};

class QueuePositionModel {
public:
    void on_book_update(const std::string& symbol,
                        double bid_price, double bid_depth,
                        double ask_price, double ask_depth,
                        uint64_t ts_ns);

    // Single-symbol top-of-book read — used by StrategyContext to feed engines.
    TopOfBook top(const std::string& symbol) const;

    OrderQueueEstimate estimate(const std::string& symbol,
                                double order_price, double order_qty,
                                bool is_buy);

    // Snapshot support
    std::unordered_map<std::string, QueueState> dump_books() const;
    void clear();
    void restore(const std::string& sym, const QueueState& st);

private:
    std::unordered_map<std::string, QueueState> books_;
    mutable std::mutex mtx_;   // mutable: const methods (dump_books, estimate) need to lock
};

}
