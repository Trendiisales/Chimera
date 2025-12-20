#pragma once
#include "shadow/ShadowTypes.hpp"
#include <atomic>

namespace Chimera {

class ShadowRecorder {
public:
    ShadowRecorder();

    void record(uint64_t seq, ShadowSource src, bool allow, double size_mult);
    void finish();

private:
    static constexpr uint64_t MAX = 1 << 20;
    DecisionSnapshot buf_[MAX];
    std::atomic<uint64_t> idx_;
};

ShadowRecorder& shadow_recorder();

}
