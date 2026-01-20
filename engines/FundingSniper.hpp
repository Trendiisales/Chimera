#pragma once

#include <atomic>
#include <cstdint>
#include <cmath>

#include "../tier3/TickData.hpp"

struct FundingSignal {
    bool is_buy;
    double confidence;
    double price;
};

class FundingSniper {
public:
    FundingSniper() = default;

    inline void setFundingRate(double r) {
        funding_rate_.store(r, std::memory_order_relaxed);
    }

    inline void setNextFundingTs(uint64_t ts_us) {
        next_funding_ts_.store(ts_us, std::memory_order_relaxed);
    }

    inline void onTick(const tier3::TickData& t) {
        uint64_t now = t.exchange_time_us;
        uint64_t next = next_funding_ts_.load(std::memory_order_relaxed);

        if (next == 0 || now + 30000000 < next) {
            return;
        }

        double rate = funding_rate_.load(std::memory_order_relaxed);

        FundingSignal s;
        s.is_buy = rate < 0.0;
        s.confidence = std::fabs(rate);
        s.price = t.midprice();

        signal_.store(true, std::memory_order_release);
        last_signal_ = s;
    }

    inline bool hasSignal() const {
        return signal_.load(std::memory_order_acquire);
    }

    inline FundingSignal consumeSignal() {
        signal_.store(false, std::memory_order_release);
        return last_signal_;
    }

private:
    std::atomic<double> funding_rate_{0.0};
    std::atomic<uint64_t> next_funding_ts_{0};

    std::atomic<bool> signal_{false};
    FundingSignal last_signal_{};
};

