#pragma once
#include "V2Runtime.hpp"
#include "../engines/StructuralMomentumEngine.hpp"
#include "../engines/CompressionBreakEngine.hpp"
#include "../engines/StopCascadeEngine.hpp"
#include "../engines/MicroImpulseEngine.hpp"

namespace ChimeraV2 {

class V2Desk {
public:
    V2Desk() {
        runtime_.register_engine(&momentum_);
        runtime_.register_engine(&compression_);
        runtime_.register_engine(&cascade_);
        runtime_.register_engine(&micro_);
    }

    void on_market_tick(const std::string& symbol,
                       double bid,
                       double ask,
                       uint64_t timestamp_ns) {
        runtime_.on_market(symbol, bid, ask, timestamp_ns);
    }

    const V2Runtime& runtime() const { return runtime_; }

private:
    StructuralMomentumEngine momentum_;
    CompressionBreakEngine compression_;
    StopCascadeEngine cascade_;
    MicroImpulseEngine micro_;
    V2Runtime runtime_;
};

}
