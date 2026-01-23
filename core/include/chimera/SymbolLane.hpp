#ifndef SYMBOLLANE_ANTIPARALYSIS_HPP
#define SYMBOLLANE_ANTIPARALYSIS_HPP

#include <string>
#include <cstdint>
#include <chrono>
#include "chimera/execution/ExchangeIO.hpp"
#include "chimera/telemetry_bridge/GuiState.hpp"
#include "chimera/survival/EdgeSurvivalFilter.hpp"
#include "chimera/survival/CostGate.hpp"
#include "chimera/execution/MarketBus.hpp"

// Nanosecond clock
inline uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

enum class Side { BUY, SELL };

struct Signal {
    bool fire = false;
    Side side;
    double confidence = 0;
};

struct Order {
    uint64_t id = 0;
    std::string symbol;
    uint32_t symbol_hash = 0;
    Side side;
    double qty = 0;
    double price = 0;
    uint64_t ts_created_ns = 0;
};

struct Fill {
    uint64_t order_id;
    double price;
    double qty;
    uint64_t ts_ack_ns;
    uint64_t ts_fill_ns;
};

class ShadowVenue {
public:
    Fill execute(const Order& o, double bid, double ask);
};

class RiskGovernor {
public:
    double max_position = 1.0;
    double daily_loss_limit = -100.0;
    double position = 0;
    double realized_pnl = 0;
    bool kill = false;

    bool allow(double qty) const {
        if (kill) return false;
        if (std::abs(position + qty) > max_position) return false;
        return true;
    }

    void on_fill(double pnl, double qty) {
        realized_pnl += pnl;
        position += qty;
        if (realized_pnl <= daily_loss_limit)
            kill = true;
    }
};

class Strategy {
public:
    Signal evaluate(const chimera::MarketTick& t) {
        Signal s;
        double spread = t.ask - t.bid;

        if (spread <= 0 || t.bid <= 0 || t.ask <= 0) return s;

        // Simple OFI-style: imbalance > 1.5x + reasonable spread
        if (t.bid_size > t.ask_size * 1.5 && spread < 5.0) {
            s.fire = true;
            s.side = Side::BUY;
            s.confidence = 0.75;
        }
        else if (t.ask_size > t.bid_size * 1.5 && spread < 5.0) {
            s.fire = true;
            s.side = Side::SELL;
            s.confidence = 0.75;
        }
        return s;
    }
};

class Lane {
public:
    explicit Lane(std::string sym, uint32_t hash);

    void on_tick(const chimera::MarketTick& tick);

    uint32_t symbolHash() const { return symbol_hash_; }
    const std::string& symbolName() const { return symbol_; }

private:
    std::string symbol_;
    uint32_t symbol_hash_;

    chimera::MarketBus market_bus_;
    chimera::EdgeSurvivalFilter survival_;
    chimera::CostGate cost_gate_;

    Strategy strategy_;
    RiskGovernor risk_;
    ShadowVenue venue_;

    uint64_t next_order_id_ = 1;
    uint64_t trade_count_ = 0;

    double last_mid_ = 0;
    int warmup_ticks_ = 0;
};

#endif // SYMBOLLANE_ANTIPARALYSIS_HPP
