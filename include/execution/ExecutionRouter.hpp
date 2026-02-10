#pragma once
#include "execution/LatencyExecutionGovernor.hpp"
#include "execution/VelocityCalculator.hpp"
#include <string>
#include <cstdint>

struct Quote {
    double bid;
    double ask;
    uint64_t ts_ms;
};

class ExecutionRouter {
public:
    ExecutionRouter();

    void on_quote(const std::string& symbol, const Quote& q);
    void on_fix_rtt(double rtt_ms, uint64_t now_ms);
    void on_loop_heartbeat(uint64_t now_ms);

    bool submit_signal(
        const std::string& symbol,
        bool is_buy,
        uint64_t signal_ts_ms,
        std::string& reject_reason
    );

    void dump_status() const;
    const LatencyExecutionGovernor& latency() const;
    double get_velocity(const std::string& symbol) const;

private:
    bool submit_xau(uint64_t signal_ts_ms, uint64_t now_ms, std::string& reject_reason);
    bool submit_xag(uint64_t signal_ts_ms, uint64_t now_ms, std::string& reject_reason);

    LatencyExecutionGovernor latency_;
    VelocityCalculator xau_velocity_;
    VelocityCalculator xag_velocity_;
};
