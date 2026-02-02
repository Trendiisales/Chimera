#pragma once

#include <atomic>
#include <string>
#include <filesystem>

#include "runtime/LiveArmSystem.hpp"
#include "execution/QueuePositionModel.hpp"
#include "execution/OrderStateMachine.hpp"
#include "execution/CancelPolicy.hpp"
#include "execution/LatencyGovernor.hpp"
#include "execution/CancelFederation.hpp"
#include "risk/GlobalRiskGovernor.hpp"
#include "control/PnLGovernor.hpp"
#include "telemetry/TelemetryState.hpp"
#include "forensics/BinaryRecorder.hpp"

namespace chimera {

// Forward declarations — pointer-held components constructed in main().
class QueueDecayGovernor;
class EdgeAttribution;
class DeskArbiter;

// Single authoritative owner of all system state.
// No globals. No statics. Everything injected from here.
// Constructed once in main(). All components receive Context& reference.
struct Context {
    std::atomic<bool> running{true};

    // Safety
    LiveArmSystem arm;

    // Execution intelligence
    QueuePositionModel queue;
    CancelPolicy cancel_policy;

    // FIX 4.3: OSM moved to Context so ContextSnapshotter can persist open orders.
    OrderStateMachine osm;

    // Risk + truth
    GlobalRiskGovernor risk;

    // PnL governor — per-strategy rolling EV kill + portfolio DD kill.
    PnLGovernor pnl;

    // Latency governor — sizes orders down on network lag, signals cancel-all
    // on hard threshold. Measures order ACK latency (submit→NEW ack).
    LatencyGovernor latency;

    // Cancel Federation — centralized kill-sweep signal.
    // Header-only (atomic CAS + pointer store). No Context& dependency.
    // Sweep runs on CORE1 in ExecutionRouter::poll().
    CancelFederation cancel_fed;

    // ---------------------------------------------------------------------------
    // Pointer-held components — constructed in main() after Context.
    // Set via ctx.X = &local_in_main. Null-checked before use.
    // ---------------------------------------------------------------------------

    // Queue Decay Governor — per-order age + queue depth decay.
    // Fires cancel federation on hard TTL breach or urgency breach.
    QueueDecayGovernor* queue_decay{nullptr};

    // Edge Attribution — per-engine execution quality tracking.
    // Kills engines that persistently leak edge.
    EdgeAttribution* edge{nullptr};

    // Desk Arbiter — cross-engine capital governance.
    // Groups engines into desks. Pauses losing desks. 2+ paused = regime event.
    DeskArbiter* desk{nullptr};

    // Telemetry
    TelemetryState telemetry;

    // ---------------------------------------------------------------------------
    // Network fault signaling — set by BinanceWSUser, read by ExecutionRouter.
    // ---------------------------------------------------------------------------
    std::atomic<bool> ws_user_alive{false};
    std::atomic<bool> needs_reconcile{false};

    // Forensics — owns the event log
    BinaryRecorder recorder;

    // B2 FIX: ensure log directory exists before opening recorder.
    Context()
        : arm(600),                              // 10 min time-lock
          cancel_policy(
              5'000'000'000ULL,                  // 5s max wait
              0.15                               // min fill probability
          ),
          recorder(ensure_log_dir("/var/log/chimera/events.bin")) {}

private:
    static const std::string& ensure_log_dir(const std::string& path) {
        static const std::string p = path;
        std::filesystem::create_directories(
            std::filesystem::path(p).parent_path());
        return p;
    }
};

}
