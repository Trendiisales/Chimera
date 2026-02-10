#include "ExecutionGovernor.h"
#include "ExecutionTimingConfig.h"
#include <algorithm>
#include <numeric>
#include <cmath>

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

    if (rtt_samples_.size() > RTT_SAMPLE_WINDOW) {
        rtt_samples_.erase(rtt_samples_.begin());
        rtt_timestamps_.erase(rtt_timestamps_.begin());
    }
    
    // Feed regime detector (event-driven)
    regime_detector_.onFixRtt(rtt_ms, t);
    
    // Alert on regime changes (once per transition)
    if (regime_detector_.regimeChanged()) {
        LatencyRegime r = regime_detector_.getRegime();
        const auto& stats = regime_detector_.getMediumStats();
        std::cout << "[NET] Latency regime changed → " << regimeToString(r)
                  << " (p95=" << stats.p95 << "ms, p99=" << stats.p99 << "ms)\n";
    }
}

LatencyRegime ExecutionGovernor::getLatencyRegime() const {
    return regime_detector_.getRegime();
}

const RollingStats& ExecutionGovernor::getLatencyStats() const {
    return regime_detector_.getMediumStats();
}

void ExecutionGovernor::onReject() {
    reject_count_++;
}

void ExecutionGovernor::onFill(double) {
    trade_count_++;
    open_legs_++;
    last_entry_ms_ = nowMs();
}

void ExecutionGovernor::onExit(double pnl) {
    open_legs_ = std::max(0, open_legs_ - 1);
    last_exit_pnl_ = pnl;

    if (pnl < 0.0)
        consecutive_negative_exits_++;
    else
        consecutive_negative_exits_ = 0;
}

static double mean(const std::vector<int>& v) {
    if (v.empty()) return 0.0;
    return double(std::accumulate(v.begin(), v.end(), 0)) / double(v.size());
}

static double stdev(const std::vector<int>& v) {
    if (v.size() < 2) return 0.0;
    double m = mean(v);
    double acc = 0.0;
    for (int x : v) acc += (x - m) * (x - m);
    return std::sqrt(acc / double(v.size()));
}

void ExecutionGovernor::updateState(uint64_t now_ms) {
    if (rtt_samples_.empty()) return;

    double avg = mean(rtt_samples_);
    
    // ═══════════════════════════════════════════════════════════
    // RULE A: PERCENTILE GATE
    // XAU entries only in p95 (fastest 95%), XAG tolerates p99
    // ═══════════════════════════════════════════════════════════
    int entry_gate = isMetalXAU() ? XAU_ENTRY_RTT_MAX_MS : XAG_ENTRY_RTT_MAX_MS;
    int block_gate = isMetalXAU() ? XAU_BLOCK_RTT_MS : XAG_BLOCK_RTT_MS;
    
    // ═══════════════════════════════════════════════════════════
    // RULE B: RISING RTT BAN (catches microbursts)
    // If RTT is rising across last 3 samples, block even if under threshold
    // ═══════════════════════════════════════════════════════════
    bool is_rising_rtt = false;
    if (rtt_samples_.size() >= RTT_RISING_WINDOW) {
        size_t n = rtt_samples_.size();
        int r1 = rtt_samples_[n-1];
        int r2 = rtt_samples_[n-2];
        int r3 = rtt_samples_[n-3];
        if (r1 > r2 && r2 > r3) {
            is_rising_rtt = true;
        }
    }
    
    // ═══════════════════════════════════════════════════════════
    // RULE C: TAIL MEMORY
    // If we saw p99 event, block entries for 500ms (tail events cluster)
    // ═══════════════════════════════════════════════════════════
    bool in_tail_memory = false;
    if (last_tail_event_ms_ > 0 && now_ms - last_tail_event_ms_ < RTT_TAIL_MEMORY_MS) {
        in_tail_memory = true;
    }
    
    // Track tail events
    if (!rtt_samples_.empty() && rtt_samples_.back() >= NET_RTT_P99_MS) {
        last_tail_event_ms_ = now_ms;
    }
    
    // ═══════════════════════════════════════════════════════════
    // STATE TRANSITIONS
    // ═══════════════════════════════════════════════════════════
    
    if (state_ != ExecState::BLOCKED) {
        bool should_block = false;
        
        // Hard RTT block
        if (avg >= block_gate) {
            should_block = true;
        }
        
        // Rising RTT block (Rule B)
        if (is_rising_rtt && avg >= XAU_CAUTION_RTT_MS) {
            should_block = true;
        }
        
        // Tail memory block (Rule C)
        if (in_tail_memory) {
            should_block = true;
        }
        
        // Loss-based blocking (XAU: 1 loss, XAG: 2 losses)
        int block_threshold = isMetalXAU() ? XAU_BLOCK_ON_LOSS_COUNT : 2;
        if (consecutive_negative_exits_ >= block_threshold) {
            should_block = true;
        }
        
        if (should_block) {
            state_ = ExecState::BLOCKED;
            state_since_ms_ = now_ms;
            last_block_ms_ = now_ms;
            return;
        }

        // CAUTION state (p90-p95 zone)
        if (avg >= XAU_CAUTION_RTT_MS) {
            state_ = ExecState::CAUTION;
            return;
        }
        
        // NORMAL state (below p90)
        state_ = ExecState::NORMAL;
    }

    // Recovery from BLOCKED state
    if (state_ == ExecState::BLOCKED) {
        if (now_ms - last_block_ms_ > GLOBAL_BLOCK_TIME_MS &&
            avg < NET_RTT_P90_MS &&
            !in_tail_memory &&
            !is_rising_rtt &&
            last_exit_pnl_ >= 0.0) {

            state_ = ExecState::NORMAL;
            state_since_ms_ = now_ms;
            consecutive_negative_exits_ = 0;
        }
    }
}

ExecDecision ExecutionGovernor::evaluate(uint64_t now_ms) {
    updateState(now_ms);

    ExecDecision d;
    d.state = state_;
    d.reason = "OK";
    
    // ═══════════════════════════════════════════════════════════
    // RULE D: EXITS ARE SACRED (always allow up to 25ms)
    // Never block exits on timing - your data proves they succeed
    // ═══════════════════════════════════════════════════════════
    d.allow_exit = true;

    if (state_ == ExecState::BLOCKED) {
        d.allow_entry = false;
        d.reason = "BLOCKED_BY_TIMING";
        return d;
    }

    if (state_ == ExecState::CAUTION) {
        // In CAUTION, enforce leg limits
        if (isMetalXAU() && open_legs_ >= XAU_MAX_OPEN_LEGS) {
            d.allow_entry = false;
            d.reason = "XAU_LEG_LIMIT_CAUTION";
            return d;
        }
        if (isMetalXAG() && open_legs_ >= XAG_MAX_OPEN_LEGS) {
            d.allow_entry = false;
            d.reason = "XAG_LEG_LIMIT_CAUTION";
            return d;
        }
    }

    d.allow_entry = true;
    return d;
}
