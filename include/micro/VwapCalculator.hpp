// =============================================================================
// VwapCalculator.hpp - v4.18.0 - SINGLE SOURCE OF TRUTH FOR VWAP
// =============================================================================
// PURPOSE: Standalone volume-weighted average price calculator.
//
// VWAP was originally a separate, authoritative micro-structure component.
// That separation regressed — VWAP became entangled with signal consumption.
// This restores it.
//
// RULES:
//   - VwapCalculator accumulates price × volume / total volume
//   - Slope is derived from consecutive VWAP samples
//   - Session-scoped: call reset() at session boundaries
//   - Nothing else computes VWAP. This is the only source.
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include <cstdint>
#include <cmath>

namespace Chimera {

class VwapCalculator {
public:
    void reset() {
        cumPxVol_ = 0.0;
        cumVol_ = 0.0;
        vwap_ = 0.0;
        slope_ = 0.0;
        lastTs_ = 0;
        sampleCount_ = 0;
    }

    void onTrade(double price, double volume, uint64_t ts_ns) {
        if (volume <= 0.0) return;

        cumPxVol_ += price * volume;
        cumVol_ += volume;

        double newVwap = cumPxVol_ / cumVol_;

        // Derive slope from consecutive VWAP values (time-stable)
        if (lastTs_ != 0 && ts_ns > lastTs_) {
            double dt_sec = static_cast<double>(ts_ns - lastTs_) / 1e9;
            if (dt_sec > 0.0) {
                slope_ = (newVwap - vwap_) / dt_sec;
            }
        }

        vwap_ = newVwap;
        lastTs_ = ts_ns;
        sampleCount_++;
    }

    // Getters
    double getVwap()    const { return vwap_; }
    double get()        const { return vwap_; }   // Alias for brevity
    double getSlope()   const { return slope_; }
    uint64_t lastTs()   const { return lastTs_; }
    int sampleCount()   const { return sampleCount_; }
    bool isWarmedUp()   const { return sampleCount_ >= 10; }

    // Computed helpers
    double distancePct(double price) const {
        if (vwap_ <= 0.0) return 0.0;
        return std::abs(price - vwap_) / vwap_;
    }

    bool priceAbove(double price) const { return price > vwap_; }
    bool priceBelow(double price) const { return price < vwap_; }

private:
    double cumPxVol_ = 0.0;
    double cumVol_   = 0.0;
    double vwap_     = 0.0;
    double slope_    = 0.0;
    uint64_t lastTs_ = 0;
    int sampleCount_ = 0;
};

} // namespace Chimera
