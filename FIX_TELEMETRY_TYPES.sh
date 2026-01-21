#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -e

echo "[CHIMERA] FIXING TELEMETRY FIELD TYPES"

############################################
# TELEMETRY BUS (STRING FIELDS)
############################################
cat > telemetry/TelemetryBus.hpp << 'TEOF'
#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <map>
#include <chrono>

struct TelemetryEvent {
    std::string type;
    std::map<std::string, std::string> fields;
    uint64_t ts;
};

class TelemetryBus {
public:
    static TelemetryBus& instance() {
        static TelemetryBus bus;
        return bus;
    }

    void publish(const std::string& type,
                 const std::map<std::string, std::string>& fields) {
        std::lock_guard<std::mutex> lock(mtx_);
        TelemetryEvent e;
        e.type = type;
        e.fields = fields;
        e.ts = now();
        events_.push_back(e);
        if (events_.size() > max_events_) {
            events_.erase(events_.begin());
        }
    }

    std::vector<TelemetryEvent> snapshot() {
        std::lock_guard<std::mutex> lock(mtx_);
        return events_;
    }

private:
    TelemetryBus() = default;

    uint64_t now() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    std::mutex mtx_;
    std::vector<TelemetryEvent> events_;
    size_t max_events_ = 10000;
};
TEOF

############################################
# REBUILD
############################################
echo "[CHIMERA] REBUILDING"
cd build
make -j$(nproc)

echo
echo "[CHIMERA] TELEMETRY FIXED"
echo "Open GUI:"
echo "http://15.168.16.103:8080"
echo
