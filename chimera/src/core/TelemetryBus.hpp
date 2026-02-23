#pragma once
#include <atomic>
#include <string>
#include <mutex>

struct TelemetrySnapshot {
    uint64_t ticks = 0;
    uint64_t orders = 0;
    uint64_t fills = 0;
    double pnl = 0.0;
    int latency_ms = 0;
    std::string regime = "INIT";
};

class TelemetryBus {
public:
    static TelemetryBus& instance();

    void tick();
    void order();
    void fill();
    void set_pnl(double v);
    void set_latency(int ms);
    void set_regime(const std::string& r);

    TelemetrySnapshot snapshot();

private:
    TelemetryBus() = default;
    std::mutex mtx_;
    TelemetrySnapshot snap_;
};
