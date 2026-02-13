#include "timing/ExecutionGovernor.h"
#include "timing/ExecutionTimingConfig.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <chrono>

using namespace timing;

static uint64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

ExecutionGovernor::ExecutionGovernor(const std::string& symbol)
: symbol_(symbol),
  state_(ExecState::NORMAL),
  state_since_ms_(nowMs()),
  reject_count_(0),
  trade_count_(0),
  open_legs_(0),
  last_exit_pnl_(0.0),
  consecutive_negative_exits_(0),
  last_entry_ms_(0),
  last_block_ms_(0),
  last_tail_event_ms_(0) {}

bool ExecutionGovernor::isMetalXAU() const {
    return symbol_ == "XAUUSD";
}

bool ExecutionGovernor::isMetalXAG() const {
    return symbol_ == "XAGUSD";
}

void ExecutionGovernor::onFixRttSample(int rtt_ms) {
    uint64_t t = nowMs();
    rtt_samples_.push_back(rtt_ms);
    rtt_timestamps_.push_back(t);
}
