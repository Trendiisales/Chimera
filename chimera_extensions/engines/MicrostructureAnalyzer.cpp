#include "MicrostructureAnalyzer.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace chimera {
namespace engines {

MicrostructureAnalyzer::MicrostructureAnalyzer() {
    m_bids.reserve(10);
    m_asks.reserve(10);
}

void MicrostructureAnalyzer::update_book(const std::vector<BookLevel>& bids,
                                        const std::vector<BookLevel>& asks) {
    m_bids = bids;
    m_asks = asks;
}

void MicrostructureAnalyzer::update_tick(double bid, double ask, 
                                        double bid_size, double ask_size) {
    m_bids.clear();
    m_asks.clear();
    
    m_bids.push_back({bid, bid_size});
    m_asks.push_back({ask, ask_size});
}

double MicrostructureAnalyzer::compute_imbalance() {
    double bid_vol = 0.0;
    double ask_vol = 0.0;

    size_t depth = std::min<size_t>(5, std::min(m_bids.size(), m_asks.size()));
    
    for (size_t i = 0; i < depth; ++i) {
        if (i < m_bids.size()) bid_vol += m_bids[i].size;
        if (i < m_asks.size()) ask_vol += m_asks[i].size;
    }

    if ((bid_vol + ask_vol) == 0.0)
        return 0.0;

    return (bid_vol - ask_vol) / (bid_vol + ask_vol);
}

double MicrostructureAnalyzer::compute_microprice() {
    if (m_bids.empty() || m_asks.empty())
        return 0.0;

    double bid = m_bids[0].price;
    double ask = m_asks[0].price;
    double bid_vol = m_bids[0].size;
    double ask_vol = m_asks[0].size;

    if ((bid_vol + ask_vol) == 0.0)
        return (bid + ask) / 2.0;

    return (ask * bid_vol + bid * ask_vol) / (bid_vol + ask_vol);
}

bool MicrostructureAnalyzer::detect_sweep() {
    if (m_bids.empty() || m_asks.empty())
        return false;

    double bid_vol = m_bids[0].size;
    double ask_vol = m_asks[0].size;

    bool sweep = (bid_vol < m_last_bid_volume * 0.5) ||
                 (ask_vol < m_last_ask_volume * 0.5);

    m_last_bid_volume = bid_vol;
    m_last_ask_volume = ask_vol;

    return sweep;
}

bool MicrostructureAnalyzer::detect_absorption() {
    if (m_bids.empty() || m_asks.empty())
        return false;

    double spread = m_asks[0].price - m_bids[0].price;
    double bid_vol = m_bids[0].size;
    double ask_vol = m_asks[0].size;

    // Tight spread + balanced volume = absorption
    if (spread < 0.2 && std::abs(bid_vol - ask_vol) < 10.0)
        return true;

    return false;
}

MicroSignal MicrostructureAnalyzer::compute_signal() {
    MicroSignal signal;

    signal.imbalance = compute_imbalance();
    signal.microprice = compute_microprice();
    signal.sweep_detected = detect_sweep();
    signal.absorption_detected = detect_absorption();

    // Track imbalance history
    m_imbalance_history.push_back(signal.imbalance);
    if (m_imbalance_history.size() > MAX_HISTORY)
        m_imbalance_history.pop_front();

    // Compute signal strength
    double strength = std::abs(signal.imbalance);

    if (signal.sweep_detected)
        strength += 0.3;

    if (signal.absorption_detected)
        strength += 0.2;

    // Boost if imbalance is persistent
    if (m_imbalance_history.size() >= 5) {
        int consistent = 0;
        double dir = signal.imbalance > 0 ? 1.0 : -1.0;
        
        for (auto imb : m_imbalance_history) {
            if ((imb > 0 && dir > 0) || (imb < 0 && dir < 0))
                consistent++;
        }
        
        if (consistent >= 4)
            strength += 0.2;
    }

    signal.signal_strength = std::clamp(strength, 0.0, 1.0);

    return signal;
}

} // namespace engines
} // namespace chimera
