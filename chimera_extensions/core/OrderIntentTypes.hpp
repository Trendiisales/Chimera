#pragma once

#include <string>
#include <cstdint>

namespace chimera {
namespace core {

enum class EngineType : uint8_t {
    HFT = 0,
    STRUCTURE = 1
};

struct OrderIntent {
    std::string symbol;
    double quantity = 0.0;
    double price = 0.0;
    bool is_buy = false;
    EngineType engine = EngineType::HFT;
    double confidence = 0.0;
    uint64_t timestamp_ns = 0;
    
    // For internal tracking
    std::string intent_id;
};

} // namespace core
} // namespace chimera
