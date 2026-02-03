#pragma once
#include <string>
#include <chrono>
#include <cstring>

#include "tier1/Tier1ExecutionRouter.hpp"
#include "tier1/CapView.hpp"
#include "tier1/SignalRing.hpp"

namespace chimera {

// ---------------------------------------------------------------------------
// Tier1BaseStrategy: Base class for strategies using lock-free submission
// 
// Key differences from old strategy base:
//   OLD: Directly called router.submit_order() → mutex lock
//   NEW: Pushes TradeSignal to lock-free ring → no blocking
// 
// New features:
//   - CapView for checking capacity before signal generation
//   - No mutex contention
//   - Backpressure handling (if ring full)
// 
// Usage:
//   class MyStrategy : public Tier1BaseStrategy {
//   public:
//       void on_tick() override {
//           if (!cap_view_.can_trade(qty))
//               return;  // Don't spam if at cap
//           
//           send(qty, price, edge_bps);
//       }
//   };
// ---------------------------------------------------------------------------

class Tier1BaseStrategy {
public:
    Tier1BaseStrategy(Tier1ExecutionRouter& router,
                      AtomicPositionGate& gate,
                      const std::string& symbol,
                      const std::string& engine_id)
        : router_(router),
          cap_view_(gate, symbol),
          symbol_(symbol),
          engine_id_(engine_id) {}

    virtual ~Tier1BaseStrategy() = default;

    // Override this in your strategy
    virtual void on_tick() = 0;

protected:
    // Send a trade signal to the router
    // Returns false if ring is full (backpressure)
    bool send(double qty, double price, double edge_bps, bool reduce_only = false) {
        // Check cap BEFORE generating signal
        if (!reduce_only && !cap_view_.can_trade(qty)) {
            // Would violate cap - don't even try
            return false;
        }
        
        // Build signal
        TradeSignal sig{};
        std::strncpy(sig.symbol, symbol_.c_str(), 11);
        sig.symbol[11] = '\0';
        std::strncpy(sig.engine_id, engine_id_.c_str(), 11);
        sig.engine_id[11] = '\0';
        sig.qty = qty;
        sig.price = price;
        sig.edge_bps = edge_bps;
        sig.ts_submit = now_ns();
        sig.reduce_only = reduce_only;
        
        // Push to ring (lock-free)
        return router_.submit(sig);
    }

    // Helpers for strategies
    double position() const {
        return cap_view_.position();
    }

    double cap() const {
        return cap_view_.cap();
    }

    double remaining() const {
        return cap_view_.remaining();
    }

    bool can_trade(double qty) const {
        return cap_view_.can_trade(qty);
    }

private:
    uint64_t now_ns() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }

    Tier1ExecutionRouter& router_;
    CapView cap_view_;
    std::string symbol_;
    std::string engine_id_;
};

} // namespace chimera
