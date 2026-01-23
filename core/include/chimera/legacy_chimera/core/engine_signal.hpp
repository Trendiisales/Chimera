#ifndef ENGINE_SIGNAL_HPP
#define ENGINE_SIGNAL_HPP

#include "chimera/core/system_state.hpp"
#include <cstdint>

struct EngineSignal {
    bool fired = false;
    Side side = Side::NONE;
    double confidence = 0.0;
    uint64_t ts_ns = 0;
    const char* source = nullptr;
};

struct DepthSignal : EngineSignal {
    double depth_ratio = 0.0;
    uint64_t vacuum_duration_ns = 0;
};

struct OFISignal : EngineSignal {
    double zscore = 0.0;
    double accel = 0.0;
};

struct LiqSignal : EngineSignal {
    double intensity = 0.0;
    bool is_long_cascade = false;
};

struct ImpulseSignal : EngineSignal {
    double displacement_bps = 0.0;
    double velocity = 0.0;
    bool open = false;
    bool buy_impulse = false;
    bool sell_impulse = false;
};

struct CascadeSignal {
    bool fired = false;
    Side side = Side::NONE;
    uint64_t ts_ns = 0;
    
    bool depth_confirmed = false;
    bool ofi_confirmed = false;
    bool liq_confirmed = false;
    bool impulse_confirmed = false;
    
    int confirmation_count = 0;
};

#endif
