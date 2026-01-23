#include "chimera/pnl/PnL.hpp"
#include "chimera/telemetry/TelemetryBus.hpp"
#include <string>

namespace chimera::pnl {

// Global PnL book (singleton)
static PnLBook g_book;

// Causal fill handler: FILL → PNL → TELEMETRY → GUI
void onFill(const FillEvent& f, telemetry::TelemetryBus& bus) {
    // Update PnL book
    g_book.onFill(f);

    // Get updated state
    const auto& s = g_book.get(f.symbol);

    // Publish causal update to telemetry bus
    bus.publish("PNL_UPDATE",
        f.symbol +
        " R=" + std::to_string(s.realized) +
        " U=" + std::to_string(s.unrealized) +
        " F=" + std::to_string(s.fees) +
        " N=" + std::to_string(s.fills));
}

// Get global PnL book
PnLBook& globalBook() {
    return g_book;
}

// Get PnL for specific symbol
const PnLState& getSymbolPnL(const std::string& symbol) {
    return g_book.get(symbol);
}

// Get all PnL states
const std::unordered_map<std::string, PnLState>& getAllPnL() {
    return g_book.all();
}

} // namespace chimera::pnl
