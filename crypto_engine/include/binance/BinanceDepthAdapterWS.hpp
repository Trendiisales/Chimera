#pragma once
#include "BinanceDepthAdapter.hpp"
#include <thread>
#include <atomic>

namespace binance {

class BinanceDepthAdapterWS : public BinanceDepthAdapter {
    std::thread th;
    std::atomic<bool> running{false};

public:
    void start(const DepthCallback& cb) override;
    void stop() override;
};

}
