#pragma once

#include "../core/OrderIntentTypes.hpp"
#include <string>

namespace chimera {
namespace engines {

class IEngine {
public:
    virtual ~IEngine() = default;
    
    virtual void on_market_data(const std::string& symbol, 
                                double bid, 
                                double ask,
                                uint64_t timestamp_ns) = 0;
    
    virtual void stop() = 0;
};

} // namespace engines
} // namespace chimera
