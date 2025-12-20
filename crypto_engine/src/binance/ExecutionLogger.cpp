#include "binance/ExecutionLogger.hpp"

#include <iostream>

namespace binance {

void ExecutionLogger::on_intent(const ExecutionIntent& intent) {
    std::cout
        << "[EXEC] symbol=" << intent.symbol
        << " side=" << static_cast<int>(intent.side)
        << " price=" << intent.price
        << " qty=" << intent.quantity
        << " ts=" << intent.ts_ns
        << "\n";
}

}
