#include "MarketStateLogger.hpp"
#include <chrono>

namespace chimera {
namespace hardening {

MarketState MarketStateLogger::capture(
    double bid,
    double ask,
    double bid_qty,
    double ask_qty,
    int64_t timestamp_ns) {
    
    MarketState s{};
    s.best_bid = bid;
    s.best_ask = ask;
    
    if (bid > 0) {
        s.spread_bps = (ask - bid) / bid * 10000.0;
    }
    
    double total = bid_qty + ask_qty;
    if (total > 0) {
        s.imbalance = (bid_qty - ask_qty) / total;
    }
    
    s.depth_top5 = bid_qty + ask_qty;
    
    // Venue quotes (placeholder - would come from multi-venue feed)
    s.venue_bid[0] = bid;
    s.venue_ask[0] = ask;
    
    s.timestamp_ns = timestamp_ns > 0 ? timestamp_ns : 
        std::chrono::system_clock::now().time_since_epoch().count();
    
    return s;
}

}} // namespace chimera::hardening
