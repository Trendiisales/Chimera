// ═══════════════════════════════════════════════════════════════════════════════
// include/runtime/ExecutionCapabilities.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.11: EXECUTION CAPABILITY MATRIX
//
// PURPOSE: Translate execution physics into allowed behaviors.
// This prevents fantasy trading tactics that don't work in your environment.
//
// CAPABILITIES:
// - allow_maker:           Can post-only orders realistically fill?
// - allow_queue_estimation: Can we estimate queue position?
// - allow_micro_repost:    Can we cancel/repost within maker timeout?
// - allow_spread_capture:  Can we capture spread profitably?
// - allow_edge_collapse:   Can we trade sub-1bps edges?
//
// LOGIC:
// - COLO:      All capabilities enabled
// - NEAR_COLO: Maker allowed, no queue estimation, repost allowed
// - WAN:       Taker only, no advanced tactics
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include "ExecutionPhysics.hpp"

namespace Chimera {
namespace Runtime {

// ─────────────────────────────────────────────────────────────────────────────
// Execution Capabilities
// ─────────────────────────────────────────────────────────────────────────────
struct ExecCapabilities {
    bool allow_maker = false;            // Post-only orders viable
    bool allow_queue_estimation = false; // Queue position meaningful
    bool allow_micro_repost = false;     // Fast cancel/repost loop
    bool allow_spread_capture = false;   // Capture spread profitably
    bool allow_edge_collapse = false;    // Trade sub-1bps edges
    bool allow_clock_sync = false;       // Exchange clock alignment
    
    // Physics classification
    ExecPhysics physics = ExecPhysics::WAN;
    
    // Confidence in capabilities (0-1)
    double confidence = 0.0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Derive Capabilities from Physics
// ─────────────────────────────────────────────────────────────────────────────
inline ExecCapabilities capabilitiesFor(ExecPhysics p, double confidence = 1.0) {
    ExecCapabilities cap;
    cap.physics = p;
    cap.confidence = confidence;
    
    switch (p) {
        case ExecPhysics::COLO:
            cap.allow_maker = true;
            cap.allow_queue_estimation = true;
            cap.allow_micro_repost = true;
            cap.allow_spread_capture = true;
            cap.allow_edge_collapse = true;
            cap.allow_clock_sync = true;
            break;
            
        case ExecPhysics::NEAR_COLO:
            cap.allow_maker = true;
            cap.allow_queue_estimation = false;  // Not reliable without colo
            cap.allow_micro_repost = true;
            cap.allow_spread_capture = true;
            cap.allow_edge_collapse = false;
            cap.allow_clock_sync = false;
            break;
            
        case ExecPhysics::UNKNOWN:
            // Conservative: treat as WAN until we know better
            cap.allow_maker = false;
            cap.allow_queue_estimation = false;
            cap.allow_micro_repost = false;
            cap.allow_spread_capture = false;
            cap.allow_edge_collapse = false;
            cap.allow_clock_sync = false;
            cap.confidence = 0.0;
            break;
            
        case ExecPhysics::WAN:
        default:
            cap.allow_maker = false;             // No queue position
            cap.allow_queue_estimation = false;
            cap.allow_micro_repost = false;
            cap.allow_spread_capture = false;
            cap.allow_edge_collapse = false;
            cap.allow_clock_sync = false;
            break;
    }
    
    return cap;
}

// ─────────────────────────────────────────────────────────────────────────────
// Downgrade Capabilities (for latency spikes)
// ─────────────────────────────────────────────────────────────────────────────
inline ExecCapabilities downgradeCapabilities(const ExecCapabilities& cap) {
    ExecCapabilities degraded = cap;
    
    switch (cap.physics) {
        case ExecPhysics::COLO:
            // Downgrade to NEAR_COLO behavior
            degraded.allow_queue_estimation = false;
            degraded.allow_edge_collapse = false;
            degraded.allow_clock_sync = false;
            degraded.confidence *= 0.7;
            break;
            
        case ExecPhysics::NEAR_COLO:
            // Downgrade to WAN behavior
            degraded.allow_maker = false;
            degraded.allow_micro_repost = false;
            degraded.allow_spread_capture = false;
            degraded.confidence *= 0.5;
            break;
            
        default:
            // Already at minimum
            break;
    }
    
    return degraded;
}

// ─────────────────────────────────────────────────────────────────────────────
// Apply Spike Degradation
// ─────────────────────────────────────────────────────────────────────────────
inline ExecCapabilities applySpikeDegradation(
    const ExecCapabilities& cap,
    bool spike_detected
) {
    if (!spike_detected) return cap;
    return downgradeCapabilities(cap);
}

// ─────────────────────────────────────────────────────────────────────────────
// Derive Capabilities from Snapshot
// ─────────────────────────────────────────────────────────────────────────────
inline ExecCapabilities capabilitiesFor(const PhysicsSnapshot& snap) {
    double confidence = snap.stable ? 1.0 : 0.7;
    if (snap.samples < 20) confidence *= 0.5;
    return capabilitiesFor(snap.physics, confidence);
}

// ─────────────────────────────────────────────────────────────────────────────
// Symbol-Specific Capability Overrides
// ─────────────────────────────────────────────────────────────────────────────
inline ExecCapabilities capabilitiesForSymbol(
    const char* symbol,
    const PhysicsSnapshot& snap
) {
    ExecCapabilities cap = capabilitiesFor(snap);
    
    // v4.12.0: CFD-only mode
    // (You cannot win queue position wars without colo + L2 + time priority)
    if (symbol[0] == 'B' || symbol[0] == 'E' || symbol[0] == 'S') {
        // BTCUSDT, ETHUSDT, SOLUSDT
        cap.allow_maker = false;
        cap.allow_queue_estimation = false;
        cap.allow_spread_capture = false;
    }
    
    return cap;
}

// ─────────────────────────────────────────────────────────────────────────────
// Capability Summary String (for logging/GUI)
// ─────────────────────────────────────────────────────────────────────────────
inline void capabilitiesStr(const ExecCapabilities& cap, char* buf, size_t len) {
    snprintf(buf, len, 
             "PHYS=%s MAKER=%c QUEUE=%c REPOST=%c SPREAD=%c EDGE=%c CONF=%.0f%%",
             execPhysicsStr(cap.physics),
             cap.allow_maker ? 'Y' : 'N',
             cap.allow_queue_estimation ? 'Y' : 'N',
             cap.allow_micro_repost ? 'Y' : 'N',
             cap.allow_spread_capture ? 'Y' : 'N',
             cap.allow_edge_collapse ? 'Y' : 'N',
             cap.confidence * 100);
}

// ─────────────────────────────────────────────────────────────────────────────
// Global Capabilities Manager
// ─────────────────────────────────────────────────────────────────────────────
class CapabilitiesManager {
public:
    static CapabilitiesManager& instance() {
        static CapabilitiesManager mgr;
        return mgr;
    }
    
    void update(const PhysicsSnapshot& snap) {
        last_snap_ = snap;
        caps_ = capabilitiesFor(snap);
    }
    
    ExecCapabilities get() const { return caps_; }
    
    ExecCapabilities getForSymbol(const char* symbol) const {
        return capabilitiesForSymbol(symbol, last_snap_);
    }
    
    ExecPhysics currentPhysics() const { return caps_.physics; }
    
    bool isColo() const { return caps_.physics == ExecPhysics::COLO; }
    bool isNearColo() const { return caps_.physics == ExecPhysics::NEAR_COLO; }
    bool isWAN() const { return caps_.physics == ExecPhysics::WAN; }
    
private:
    CapabilitiesManager() = default;
    PhysicsSnapshot last_snap_;
    ExecCapabilities caps_;
};

inline CapabilitiesManager& getCapabilitiesManager() {
    return CapabilitiesManager::instance();
}

} // namespace Runtime
} // namespace Chimera
