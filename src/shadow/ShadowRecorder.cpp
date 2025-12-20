#include "shadow/ShadowRecorder.hpp"
#include <iostream>

using namespace Chimera;

static ShadowRecorder g_shadow;

ShadowRecorder::ShadowRecorder() : idx_(0) {}

ShadowRecorder& Chimera::shadow_recorder() {
    return g_shadow;
}

void ShadowRecorder::record(uint64_t seq, ShadowSource src, bool allow, double size_mult) {
    uint64_t i = idx_.fetch_add(1, std::memory_order_relaxed);
    if (i >= MAX) return;

    buf_[i].seq = seq;
    buf_[i].source = static_cast<uint8_t>(src);
    buf_[i].allow = allow ? 1 : 0;
    buf_[i].size_mult = size_mult;
}

void ShadowRecorder::finish() {
    uint64_t n = idx_.load(std::memory_order_relaxed);

    uint64_t mismatches = 0;

    for (uint64_t i = 0; i + 1 < n; i += 2) {
        const DecisionSnapshot& a = buf_[i];
        const DecisionSnapshot& b = buf_[i + 1];

        if (a.allow != b.allow || a.size_mult != b.size_mult) {
            mismatches++;
            std::cerr
                << "DIVERGENCE seq=" << a.seq
                << " live(allow=" << int(a.allow)
                << " size=" << a.size_mult
                << ") replay(allow=" << int(b.allow)
                << " size=" << b.size_mult
                << ")\n";
        }
    }

    std::cerr << "SHADOW SUMMARY mismatches=" << mismatches << " total_pairs=" << (n / 2) << "\n";
}
