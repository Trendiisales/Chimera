#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <chrono>
#include "LatencyRegime.h"

enum class ExecState {
    NORMAL,
    CAUTION,
    BLOCKED
};

struct ExecDecision {
    bool allow_entry;
    bool allow_exit;
    ExecState state;
    LatencyRegime latency_regime;
    const char* reason;
};

class ExecutionGovernor {
public:
    ExecutionGovernor(const std::string& symbol);

    void onFixRttSample(int rtt_ms);
    void onReject();
    void onFill(double pnl);
    void onExit(double pnl);

    ExecDecision evaluate(uint64_t now_ms);
    
    LatencyRegime getLatencyRegime() const;
    const RollingStats& getLatencyStats() const;

private:
    void updateState(uint64_t now_ms);
    bool isMetalXAU() const;
    bool isMetalXAG() const;

    std::string symbol_;

    ExecState state_;
    uint64_t state_since_ms_;
    
    LatencyRegimeDetector regime_detector_;

    std::vector<int> rtt_samples_;
    std::vector<uint64_t> rtt_timestamps_;

    int reject_count_;
    int trade_count_;
    int open_legs_;

    double last_exit_pnl_;
    int consecutive_negative_exits_;

    uint64_t last_entry_ms_;
    uint64_t last_block_ms_;
    uint64_t last_tail_event_ms_;
};
