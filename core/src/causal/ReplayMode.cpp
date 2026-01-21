#include "chimera/causal/ReplayMode.hpp"

namespace chimera {

void ReplayMode::enable() {
    enabled_.store(true);
}

void ReplayMode::disable() {
    enabled_.store(false);
}

bool ReplayMode::isEnabled() const {
    return enabled_.load();
}

void ReplayMode::setFrozenTimestamp(int64_t ts_ms) {
    frozen_timestamp_.store(ts_ms);
}

int64_t ReplayMode::frozenTimestamp() const {
    return frozen_timestamp_.load();
}

void ReplayMode::setFrozenLatency(
    const std::string& venue,
    double rtt_ms,
    double ws_lag_ms
) {
    frozen_latency_[venue] = {rtt_ms, ws_lag_ms};
}

bool ReplayMode::hasFrozenLatency(const std::string& venue) const {
    return frozen_latency_.count(venue) > 0;
}

double ReplayMode::frozenRTT(const std::string& venue) const {
    auto it = frozen_latency_.find(venue);
    if (it == frozen_latency_.end()) return 0.0;
    return it->second.rtt_ms;
}

double ReplayMode::frozenWSLag(const std::string& venue) const {
    auto it = frozen_latency_.find(venue);
    if (it == frozen_latency_.end()) return 0.0;
    return it->second.ws_lag_ms;
}

void ReplayMode::setFrozenVenue(
    const std::string& symbol,
    const std::string& venue
) {
    frozen_venues_[symbol] = venue;
}

bool ReplayMode::hasFrozenVenue(const std::string& symbol) const {
    return frozen_venues_.count(symbol) > 0;
}

std::string ReplayMode::frozenVenue(const std::string& symbol) const {
    auto it = frozen_venues_.find(symbol);
    if (it == frozen_venues_.end()) return "";
    return it->second;
}

void ReplayMode::setFrozenCapital(
    const std::string& engine,
    double weight
) {
    frozen_capital_[engine] = weight;
}

bool ReplayMode::hasFrozenCapital(const std::string& engine) const {
    return frozen_capital_.count(engine) > 0;
}

double ReplayMode::frozenCapital(const std::string& engine) const {
    auto it = frozen_capital_.find(engine);
    if (it == frozen_capital_.end()) return 0.0;
    return it->second;
}

}
