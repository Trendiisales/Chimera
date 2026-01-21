#include "chimera/execution/Telemetry.hpp"

namespace chimera {

void Telemetry::logTrade(const TradeRecord& rec) {
    trade_log.push_back(rec);
}

void Telemetry::logDecision(const DecisionTrace& trace) {
    decision_log.push_back(trace);
}

const std::vector<TradeRecord>&
Telemetry::trades() const {
    return trade_log;
}

const std::vector<DecisionTrace>&
Telemetry::decisions() const {
    return decision_log;
}

}
