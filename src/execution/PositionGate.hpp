#pragma once
#include <string>
#include <mutex>
#include <unordered_map>
#include <cmath>

namespace chimera {

// FIXED: PositionGate that ALWAYS allows risk-reducing trades
class PositionGate {
public:
    explicit PositionGate(double max_position_per_symbol)
        : max_position_(max_position_per_symbol) {}

    // CRITICAL FIX: Allow exits even when at cap
    // Returns true if allowed, false if would exceed cap
    bool would_violate(const std::string& symbol, double signed_qty) const {
        std::lock_guard<std::mutex> lock(mtx_);
        
        auto it = positions_.find(symbol);
        double current = (it != positions_.end()) ? it->second : 0.0;
        double next = current + signed_qty;
        
        // ALWAYS allow risk-reducing trades
        if (std::fabs(next) < std::fabs(current)) {
            return false;  // Exit allowed
        }
        
        // Block only if INCREASING exposure beyond cap
        return std::fabs(next) > max_position_;
    }

    void reserve(const std::string& symbol, double signed_qty) {
        std::lock_guard<std::mutex> lock(mtx_);
        positions_[symbol] += signed_qty;
    }

    double get_position(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = positions_.find(symbol);
        return (it != positions_.end()) ? it->second : 0.0;
    }

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
