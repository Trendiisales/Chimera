#pragma once
#include "BinanceTypes.hpp"
#include <functional>

namespace binance {

using DepthCallback = std::function<void(const DepthDelta&)>;

class BinanceDepthAdapter {
public:
    virtual ~BinanceDepthAdapter() = default;
    virtual void start(const DepthCallback& cb) = 0;
    virtual void stop() = 0;
};

}
