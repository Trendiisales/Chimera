// ============================================================================
// crypto_engine/include/micro/CentralMicroEngine.hpp
// ============================================================================
// Production-grade central micro engine for Binance
// ============================================================================
#pragma once

#include <cstdint>
#include <atomic>
#include "MicroEngines_CRTP.hpp"

struct MicroSnapshot {
    double obi;
    double microprice;
    double trade_imbalance;
    double vol_burst;
    uint64_t ts_ns;
};

template<typename Engines>
class CentralMicroEngine {
public:
    inline void on_book(
        double bid_px, double bid_sz,
        double ask_px, double ask_sz,
        uint64_t ts_ns
    ) {
        engines_.on_book(bid_px, bid_sz, ask_px, ask_sz, ts_ns);
        snapshot_.ts_ns = ts_ns;
        snapshot_.obi = engines_.obi.value();
        snapshot_.microprice = engines_.microprice.value();
    }

    inline void on_trade(
        bool is_buy,
        double qty,
        uint64_t ts_ns
    ) {
        engines_.on_trade(is_buy, qty, ts_ns);
        snapshot_.trade_imbalance = engines_.tfi.value();
    }

    inline void on_price(
        double price,
        uint64_t ts_ns
    ) {
        engines_.on_price(price, ts_ns);
        snapshot_.vol_burst = engines_.vol.value();
    }

    inline const MicroSnapshot& snapshot() const {
        return snapshot_;
    }

private:
    Engines engines_;
    MicroSnapshot snapshot_{};
};

// Convenience typedef for Binance
using BinanceCentralMicro = CentralMicroEngine<BinanceMicroEngines>;
