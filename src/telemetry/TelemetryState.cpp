#include "telemetry/TelemetryState.hpp"
#include <sstream>

using namespace chimera;

void TelemetryState::set_uptime(uint64_t sec) {
    std::lock_guard<std::mutex> lock(mtx_);
    uptime_sec_ = sec;
}

void TelemetryState::set_latency(uint64_t us) {
    std::lock_guard<std::mutex> lock(mtx_);
    latency_us_ = us;
}

void TelemetryState::set_drift(bool v) {
    std::lock_guard<std::mutex> lock(mtx_);
    drift_ = v;
}

// FIX 2.3: Single atomic increment — no read-modify-write race.
void TelemetryState::increment_throttle_block() {
    throttle_blocks_.fetch_add(1);
}

uint64_t TelemetryState::throttle_blocks() const {
    return throttle_blocks_.load();
}

void TelemetryState::increment_risk_block() {
    risk_blocks_.fetch_add(1);
}

uint64_t TelemetryState::risk_blocks() const {
    return risk_blocks_.load();
}

void TelemetryState::increment_fills() {
    total_fills_.fetch_add(1);
}

uint64_t TelemetryState::total_fills() const {
    return total_fills_.load();
}

void TelemetryState::update_symbol(const std::string& sym, double qty, double notional) {
    std::lock_guard<std::mutex> lock(mtx_);
    symbols_[sym].position_qty   = qty;
    symbols_[sym].notional       = notional;
    symbols_[sym].last_update_ns = latency_us_;
}

std::string TelemetryState::to_json() const {
    std::lock_guard<std::mutex> lock(mtx_);
    std::ostringstream out;
    out << "{\"uptime\":" << uptime_sec_
        << ",\"latency_us\":" << latency_us_
        << ",\"drift\":" << (drift_ ? "true" : "false")
        << ",\"throttle_blocks\":" << throttle_blocks_.load()
        << ",\"risk_blocks\":" << risk_blocks_.load()
        << ",\"total_fills\":" << total_fills_.load()
        << ",\"symbols\":{";

    bool first = true;
    for (const auto& kv : symbols_) {
        if (!first) out << ",";
        first = false;
        out << "\"" << kv.first << "\":{"
            << "\"qty\":" << kv.second.position_qty
            << ",\"notional\":" << kv.second.notional
            << ",\"last_ns\":" << kv.second.last_update_ns << "}";
    }
    out << "}}";
    return out.str();
}

std::string TelemetryState::to_prometheus() const {
    std::lock_guard<std::mutex> lock(mtx_);
    std::ostringstream out;
    out << "chimera_uptime " << uptime_sec_ << "\n"
        << "chimera_latency_us " << latency_us_ << "\n"
        << "chimera_drift " << (drift_ ? 1 : 0) << "\n"
        << "chimera_throttle_blocks " << throttle_blocks_.load() << "\n"
        << "chimera_risk_blocks " << risk_blocks_.load() << "\n";

    for (const auto& kv : symbols_) {
        out << "chimera_position_qty{symbol=\"" << kv.first << "\"} "
            << kv.second.position_qty << "\n"
            << "chimera_notional{symbol=\"" << kv.first << "\"} "
            << kv.second.notional << "\n";
    }
    return out.str();
}
