// ═══════════════════════════════════════════════════════════════════════════════
// include/execution/TransportSelector.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.11: FIX vs NATIVE API TRANSPORT SELECTION
//
// PURPOSE: Auto-select the fastest execution path per symbol, per venue.
// Never assume FIX is slower or native is better - MEASURE and choose.
//
// METRICS:
// - ACK latency p95
// - Cancel latency p95
// - Reject rate
//
// DECISION:
// - Score each transport by weighted cost
// - Choose lower cost automatically
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstring>
#include <algorithm>

namespace Chimera {
namespace Execution {

// ─────────────────────────────────────────────────────────────────────────────
// Transport Type
// ─────────────────────────────────────────────────────────────────────────────
enum class Transport : uint8_t {
    FIX    = 0,  // FIX protocol
    NATIVE = 1,  // Native REST/WebSocket API
    AUTO   = 2   // Auto-select
};

inline const char* transportStr(Transport t) {
    switch (t) {
        case Transport::FIX:    return "FIX";
        case Transport::NATIVE: return "NATIVE";
        case Transport::AUTO:   return "AUTO";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Transport Statistics
// ─────────────────────────────────────────────────────────────────────────────
struct TransportStats {
    Transport transport = Transport::NATIVE;
    double ack_p95_ms = 0.0;
    double cancel_p95_ms = 0.0;
    double reject_rate = 0.0;
    size_t samples = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Transport Score Weights
// ─────────────────────────────────────────────────────────────────────────────
struct TransportWeights {
    double ack_weight = 0.6;          // ACK latency importance
    double cancel_weight = 0.2;       // Cancel latency importance
    double reject_weight = 40.0;      // Reject rate penalty
};

// ─────────────────────────────────────────────────────────────────────────────
// Compute Transport Cost (lower is better)
// ─────────────────────────────────────────────────────────────────────────────
inline double transportCost(
    const TransportStats& stats,
    const TransportWeights& w = TransportWeights{}
) {
    return stats.ack_p95_ms * w.ack_weight +
           stats.cancel_p95_ms * w.cancel_weight +
           stats.reject_rate * w.reject_weight;
}

// ─────────────────────────────────────────────────────────────────────────────
// Choose Transport
// ─────────────────────────────────────────────────────────────────────────────
struct TransportDecision {
    Transport choice = Transport::NATIVE;
    double cost_chosen = 0.0;
    double cost_alternative = 0.0;
    double advantage_pct = 0.0;
};

inline TransportDecision chooseTransport(
    const TransportStats& fix,
    const TransportStats& native,
    const TransportWeights& w = TransportWeights{}
) {
    TransportDecision dec;
    
    double fix_cost = transportCost(fix, w);
    double nat_cost = transportCost(native, w);
    
    if (nat_cost < fix_cost) {
        dec.choice = Transport::NATIVE;
        dec.cost_chosen = nat_cost;
        dec.cost_alternative = fix_cost;
    } else {
        dec.choice = Transport::FIX;
        dec.cost_chosen = fix_cost;
        dec.cost_alternative = nat_cost;
    }
    
    // Calculate advantage percentage
    if (dec.cost_alternative > 0) {
        dec.advantage_pct = (dec.cost_alternative - dec.cost_chosen) / dec.cost_alternative * 100.0;
    }
    
    return dec;
}

// ─────────────────────────────────────────────────────────────────────────────
// Transport Manager
// ─────────────────────────────────────────────────────────────────────────────
class TransportManager {
public:
    // Session buckets for segmented stats
    enum class Session : uint8_t {
        ASIA = 0,
        LONDON = 1,
        NEW_YORK = 2,
        OVERLAP = 3,
        NUM_SESSIONS = 4
    };
    
    static Session currentSession() {
        std::time_t now = std::time(nullptr);
        std::tm* utc = std::gmtime(&now);
        int hour = utc ? utc->tm_hour : 12;
        
        if (hour >= 14 && hour < 16) return Session::OVERLAP;
        if (hour >= 8 && hour < 16) return Session::LONDON;
        if (hour >= 14 && hour < 21) return Session::NEW_YORK;
        if (hour >= 0 && hour < 8) return Session::ASIA;
        return Session::ASIA;
    }
    
    void updateFIX(double ack_ms, double cancel_ms, bool rejected) {
        Session sess = currentSession();
        updateStats(fix_stats_[static_cast<int>(sess)], fix_samples_[static_cast<int>(sess)],
                    fix_rejects_[static_cast<int>(sess)], ack_ms, cancel_ms, rejected);
        fix_stats_[static_cast<int>(sess)].transport = Transport::FIX;
    }
    
    void updateNative(double ack_ms, double cancel_ms, bool rejected) {
        Session sess = currentSession();
        updateStats(native_stats_[static_cast<int>(sess)], native_samples_[static_cast<int>(sess)],
                    native_rejects_[static_cast<int>(sess)], ack_ms, cancel_ms, rejected);
        native_stats_[static_cast<int>(sess)].transport = Transport::NATIVE;
    }
    
    TransportDecision choose() const {
        Session sess = currentSession();
        return chooseTransport(fix_stats_[static_cast<int>(sess)], 
                               native_stats_[static_cast<int>(sess)]);
    }
    
    // Choose with session override
    TransportDecision chooseForSession(Session sess) const {
        return chooseTransport(fix_stats_[static_cast<int>(sess)], 
                               native_stats_[static_cast<int>(sess)]);
    }
    
    Transport best() const {
        return choose().choice;
    }
    
    const TransportStats& fixStats() const { 
        return fix_stats_[static_cast<int>(currentSession())]; 
    }
    
    const TransportStats& nativeStats() const { 
        return native_stats_[static_cast<int>(currentSession())]; 
    }
    
    // Get aggregate stats across all sessions
    TransportStats aggregateFIXStats() const {
        TransportStats agg{Transport::FIX};
        size_t total_samples = 0;
        double ack_sum = 0, cancel_sum = 0, reject_sum = 0;
        
        for (int i = 0; i < static_cast<int>(Session::NUM_SESSIONS); i++) {
            if (fix_samples_[i] > 0) {
                ack_sum += fix_stats_[i].ack_p95_ms * fix_samples_[i];
                cancel_sum += fix_stats_[i].cancel_p95_ms * fix_samples_[i];
                reject_sum += fix_rejects_[i];
                total_samples += fix_samples_[i];
            }
        }
        
        if (total_samples > 0) {
            agg.ack_p95_ms = ack_sum / total_samples;
            agg.cancel_p95_ms = cancel_sum / total_samples;
            agg.reject_rate = reject_sum / total_samples;
            agg.samples = total_samples;
        }
        return agg;
    }
    
private:
    TransportStats fix_stats_[4]{};
    TransportStats native_stats_[4]{};
    size_t fix_samples_[4] = {0, 0, 0, 0};
    size_t native_samples_[4] = {0, 0, 0, 0};
    size_t fix_rejects_[4] = {0, 0, 0, 0};
    size_t native_rejects_[4] = {0, 0, 0, 0};
    
    void updateStats(TransportStats& stats, size_t& samples, size_t& rejects,
                     double ack_ms, double cancel_ms, bool rejected) {
        samples++;
        if (samples > 1) {
            stats.ack_p95_ms = 0.95 * stats.ack_p95_ms + 0.05 * ack_ms;
            stats.cancel_p95_ms = 0.95 * stats.cancel_p95_ms + 0.05 * cancel_ms;
        } else {
            stats.ack_p95_ms = ack_ms;
            stats.cancel_p95_ms = cancel_ms;
        }
        if (rejected) rejects++;
        stats.reject_rate = static_cast<double>(rejects) / samples;
        stats.samples = samples;
    }
};

} // namespace Execution
} // namespace Chimera
