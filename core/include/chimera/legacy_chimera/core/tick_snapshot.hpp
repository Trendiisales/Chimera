#ifndef TICK_SNAPSHOT_HPP
#define TICK_SNAPSHOT_HPP

#include <atomic>
#include <cstdint>

struct DepthState {
    double bid_depth = 0.0;
    double ask_depth = 0.0;
    double depth_ratio = 1.0;
    double replenish_rate = 0.0;
    bool collapse = false;
    uint64_t collapse_start_ns = 0;
    uint64_t collapse_duration_ns = 0;
};

struct OFIState {
    double zscore = 0.0;
    double accel = 0.0;
    bool forced_buy = false;
    bool forced_sell = false;
};

struct LiqState {
    double intensity = 0.0;
    double long_intensity = 0.0;
    double short_intensity = 0.0;
    bool spike = false;
    bool long_cascade = false;
    bool short_cascade = false;
};

struct ImpulseState {
    double displacement_bps = 0.0;
    double velocity = 0.0;
    bool open = false;
    bool buy_impulse = false;
    bool sell_impulse = false;
};

struct TickSnapshot {
    double price = 0.0;
    double bid = 0.0;
    double ask = 0.0;
    double spread_bps = 0.0;
    uint64_t ts_ns = 0;
    
    DepthState depth;
    OFIState ofi;
    LiqState liq;
    ImpulseState impulse;
};

class SnapshotPublisher {
public:
    void publish(const TickSnapshot& snap) {
        TickSnapshot* new_snap = new TickSnapshot(snap);
        TickSnapshot* old = current_.exchange(new_snap, std::memory_order_release);
        
        if (old && old != &empty_) {
            pending_delete_.store(old, std::memory_order_relaxed);
        }
        
        TickSnapshot* to_delete = pending_delete_.exchange(nullptr, std::memory_order_relaxed);
        delete to_delete;
    }

    const TickSnapshot* read() const {
        return current_.load(std::memory_order_acquire);
    }

    ~SnapshotPublisher() {
        TickSnapshot* curr = current_.load();
        if (curr && curr != &empty_) delete curr;
        TickSnapshot* pend = pending_delete_.load();
        delete pend;
    }

private:
    TickSnapshot empty_;
    std::atomic<TickSnapshot*> current_{&empty_};
    std::atomic<TickSnapshot*> pending_delete_{nullptr};
};

#endif
