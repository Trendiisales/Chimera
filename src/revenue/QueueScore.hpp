#pragma once
#include <unordered_map>
#include <string>
#include <mutex>
#include <chrono>

namespace chimera {

// ---------------------------------------------------------------------------
// QueueScore — Maker order quality assessment
//
// Tracks the "health" of pending maker orders based on:
// - Time in book (age)
// - Volume traded through our price level (queue position erosion)
//
// Low-scoring orders (old + lots of trades through) should be canceled
// and reposted to improve fill probability.
//
// This prevents capital from being tied up in "dead" maker orders that
// will never fill, improving capital efficiency.
//
// CRITICAL SAFETY: Never cancel reduce-only or unwind orders. These are
// risk management orders that must execute regardless of queue position.
// ---------------------------------------------------------------------------

struct QueueState {
    uint64_t submit_time_ns;     // When order was submitted
    double traded_through_volume; // Cumulative volume at our price level
    double our_size;              // Our order size
};

class QueueScore {
public:
    QueueScore();
    
    // Register a new maker order
    void on_submit(const std::string& client_id, 
                   double size,
                   uint64_t timestamp_ns);
    
    // Update when volume trades through our price level
    void on_trade_through(const std::string& client_id, double volume);
    
    // Calculate current queue score for an order
    // Returns score in range [0, 1] where:
    // - 1.0 = perfect (young order, no trades through)
    // - 0.0 = dead (old order, lots of trades through)
    double calculate_score(const std::string& client_id, 
                          uint64_t current_time_ns) const;
    
    // Check if order should be canceled and replaced
    // threshold = score below which order should be replaced (default 0.3)
    // SAFETY: Never returns true for PORTFOLIO_SKEW orders (unwinders)
    bool should_replace(const std::string& client_id,
                       uint64_t current_time_ns,
                       double threshold = 0.3) const;
    
    // Check if this engine's orders should be subject to queue scoring
    // Returns false for risk management engines (unwinders)
    static bool should_score_engine(const std::string& engine_id) {
        // Never score or cancel reduce-only/unwind orders
        return engine_id != "PORTFOLIO_SKEW";
    }
    
    // Remove order from tracking (on fill or cancel)
    void on_done(const std::string& client_id);

private:
    mutable std::mutex mtx_;
    std::unordered_map<std::string, QueueState> orders_;
    
    // Decay parameters for score calculation
    static constexpr double AGE_DECAY_NS = 5'000'000'000.0;  // 5 seconds
    static constexpr double VOLUME_DECAY = 10.0;  // Normalized volume threshold
};

} // namespace chimera
