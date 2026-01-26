#pragma once
#include <cstdint>
#include <string>

class DepthGapDetector {
public:
    DepthGapDetector(const std::string& symbol)
        : symbol_(symbol),
          last_update_id_(0),
          initialized_(false) {}

    // Returns true if OK, false if GAP detected
    bool on_update(uint64_t first_id, uint64_t final_id, std::string& reason_out) {
        if (!initialized_) {
            last_update_id_ = final_id;
            initialized_ = true;
            return true;
        }

        // Binance rule: first_id must be last_update_id + 1
        if (first_id != last_update_id_ + 1) {
            reason_out = "DEPTH GAP " + symbol_ +
                         " expected=" + std::to_string(last_update_id_ + 1) +
                         " got=" + std::to_string(first_id);
            return false;
        }

        last_update_id_ = final_id;
        return true;
    }

private:
    std::string symbol_;
    uint64_t last_update_id_;
    bool initialized_;
};
