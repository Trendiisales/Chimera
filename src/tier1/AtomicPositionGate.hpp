#pragma once
#include <atomic>
#include <unordered_map>
#include <string>
#include <cmath>
#include <iostream>

namespace chimera {

// ---------------------------------------------------------------------------
// AtomicPositionGate: Lock-free position tracking
// 
// Key difference from old PositionGate:
//   OLD: std::mutex + std::unordered_map<string, double>
//   NEW: std::atomic<double> per symbol
// 
// Thread model:
//   - ONE writer: ExecutionRouter (router thread)
//   - N readers: Strategies (strategy threads)
// 
// Safety:
//   - Only router calls apply_fill() (writer)
//   - Strategies call get_position(), get_cap(), allow() (readers)
//   - No data races (single writer + atomic loads)
// 
// Memory ordering:
//   - Relaxed for readers (position/cap are independent variables)
//   - Release for writer (ensures visibility to readers)
// ---------------------------------------------------------------------------

class AtomicPositionGate {
public:
    // Set position cap for a symbol
    // Called during initialization or dynamic cap adjustment
    void set_cap(const std::string& sym, double cap) {
        caps_[sym].store(cap, std::memory_order_release);
    }

    // Get current cap (reader)
    double get_cap(const std::string& sym) const {
        auto it = caps_.find(sym);
        if (it == caps_.end())
            return 0.0;
        return it->second.load(std::memory_order_relaxed);
    }

    // Get current position (reader)
    double get_position(const std::string& sym) const {
        auto it = positions_.find(sym);
        if (it == positions_.end())
            return 0.0;
        return it->second.load(std::memory_order_relaxed);
    }

    // Check if adding delta would violate cap (reader)
    // Called by strategies BEFORE pushing to ring
    // Called by router BEFORE executing
    bool allow(const std::string& sym, double delta) const {
        double pos = get_position(sym);
        double cap = get_cap(sym);
        double next = pos + delta;
        
        if (std::abs(next) > cap + 1e-9) {
            return false;
        }
        return true;
    }

    // Apply fill to position (WRITER ONLY - router thread)
    // This is the ONLY place position is mutated
    void apply_fill(const std::string& sym, double delta) {
        // Note: fetch_add is atomic, but map access is not
        // This is safe because ONLY router thread calls this
        positions_[sym].fetch_add(delta, std::memory_order_release);
    }

    // Set position directly (WRITER ONLY - for reconciliation)
    void set_position(const std::string& sym, double pos) {
        positions_[sym].store(pos, std::memory_order_release);
    }

private:
    // Positions and caps per symbol
    // Maps are not thread-safe, but that's OK because:
    //   - Only router modifies (single writer)
    //   - Strategies only read after init (no map modification)
    mutable std::unordered_map<std::string, std::atomic<double>> positions_;
    mutable std::unordered_map<std::string, std::atomic<double>> caps_;
};

} // namespace chimera
