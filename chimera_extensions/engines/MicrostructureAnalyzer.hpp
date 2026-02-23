#pragma once

#include <vector>
#include <deque>

namespace chimera {
namespace engines {

struct BookLevel {
    double price = 0.0;
    double size = 0.0;
};

struct MicroSignal {
    double imbalance = 0.0;
    double microprice = 0.0;
    bool sweep_detected = false;
    bool absorption_detected = false;
    double signal_strength = 0.0;
};

class MicrostructureAnalyzer {
public:
    MicrostructureAnalyzer();

    void update_book(const std::vector<BookLevel>& bids,
                    const std::vector<BookLevel>& asks);
    
    void update_tick(double bid, double ask, double bid_size, double ask_size);

    MicroSignal compute_signal();

private:
    double compute_imbalance();
    double compute_microprice();
    bool detect_sweep();
    bool detect_absorption();

private:
    std::vector<BookLevel> m_bids;
    std::vector<BookLevel> m_asks;

    double m_last_bid_volume = 0.0;
    double m_last_ask_volume = 0.0;
    
    std::deque<double> m_imbalance_history;
    static constexpr size_t MAX_HISTORY = 20;
};

} // namespace engines
} // namespace chimera
