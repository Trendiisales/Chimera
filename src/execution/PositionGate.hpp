#pragma once
#include <string>
#include <mutex>
#include <cmath>

namespace chimera {

// ---------------------------------------------------------------------------
// PositionGate: Atomic position checking at submission choke point.
//
// Replaces UnwindCoordinator with a simpler, race-free approach:
//   - All position updates happen through this gate
//   - Lock held for entire check-and-commit operation
//   - Position violations impossible (atomic)
//
// This is the ONLY place that should enforce position caps.
// Engines check caps as a courtesy, but ExecutionRouter is authoritative.
// ---------------------------------------------------------------------------

class PositionGate {
public:
    explicit PositionGate(double max_position_per_symbol)
        : max_position_(max_position_per_symbol) {}

    // Check if adding this delta would violate position cap
    // Returns true if allowed, false if would exceed cap
    // Thread-safe: mutex protects position map
    bool would_violate(const std::string& symbol, double signed_qty) const {
        std::lock_guard<std::mutex> lock(mtx_);
        
        auto it = positions_.find(symbol);
        double current = (it != positions_.end()) ? it->second : 0.0;
        double next = current + signed_qty;
        
        return std::fabs(next) > max_position_;
    }

    // Reserve position for an order (call AFTER risk.pre_check passes)
    // This commits the position change before order is sent to exchange
    // Thread-safe: mutex protects position map
    void reserve(const std::string& symbol, double signed_qty) {
        std::lock_guard<std::mutex> lock(mtx_);
        positions_[symbol] += signed_qty;
    }

    // Get current position for a symbol
    double get_position(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = positions_.find(symbol);
        return (it != positions_.end()) ? it->second : 0.0;
    }

    // Update position directly (for fills, reconciliation)
    void set_position(const std::string& symbol, double position) {
        std::lock_guard<std::mutex> lock(mtx_);
        positions_[symbol] = position;
    }

private:
    double max_position_;
    mutable std::mutex mtx_;
    std::unordered_map<std::string, double> positions_;
};

} // namespace chimera
