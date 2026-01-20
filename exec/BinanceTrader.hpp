#pragma once
#include <string>
#include <atomic>

class KillSwitch;
class ExchangeInfoCache;

class BinanceTrader {
public:
    BinanceTrader(const std::string&,
                  const std::string&,
                  KillSwitch&,
                  ExchangeInfoCache&) {}

    std::string placeLimit(const std::string&,
                           bool,
                           double,
                           double,
                           bool,
                           const std::string&) {
        last_latency_us_.store(50, std::memory_order_relaxed);
        return "stub";
    }

    void cancel(const std::string&, const std::string&) {}
    void pollFills(const std::string&) {}
    void flattenAll(const std::string&) {}

    uint64_t lastOrderLatencyUs() const {
        return last_latency_us_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<uint64_t> last_latency_us_{0};
};
