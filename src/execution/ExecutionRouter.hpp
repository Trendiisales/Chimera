#pragma once
#include <string>
#include <atomic>
#include <unordered_set>
#include <thread>
#include <chrono>
#include <memory>

#include "runtime/Context.hpp"
#include "execution/ExecutionThrottle.hpp"
#include "execution/CancelReplaceCoalescer.hpp"

// Tier1+Tier2 components
#include "tier1/SignalRing.hpp"
#include "tier1/AtomicPositionGate.hpp"
#include "tier1/SymbolTable.hpp"
#include "tier2/ElasticCapital.hpp"
#include "tier2/FillToxicityFilter.hpp"
#include "tier2/LatencyEVGate.hpp"
#include "tier2/MakerQueueHealth.hpp"

// Tier6-8 Hunt Mode Controls
#include "control/PositionGate.hpp"
#include "control/DriftFilter.hpp"
#include "control/FundingEngine.hpp"
#include "control/VolatilityRegime.hpp"
#include "control/SessionEngine.hpp"
#include "control/ExecutionAlpha.hpp"
#include "control/Microprice.hpp"
#include "control/QueueModel.hpp"

// Profitability Components
#include "control/AvellanedaStoikov.hpp"
#include "control/AdverseSelectionDetector.hpp"
#include "control/VPINDetector.hpp"

// USD-based Capital Management (from document 4)
#include "router/PositionGate.hpp"
#include "router/SmartExecutor.hpp"

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

namespace chimera {

class BinanceRestClient;
class BinanceWSExecution;

class ExecutionRouter {
public:
    explicit ExecutionRouter(std::shared_ptr<Context> ctx);
    ~ExecutionRouter();

    void set_ws_exec(BinanceWSExecution* ws_exec) { ws_exec_ = ws_exec; }

    // Wire REST client for cancel federation sweep ONLY (fire-and-forget fallback).
    // Separate from ws_exec_ — sweep runs when system is dying, latency irrelevant.
    void set_rest_client(BinanceRestClient* client) { rest_client_ = client; }

    bool submit_order(const std::string& client_id,
                      const std::string& symbol,
                      double price, double qty,
                      const std::string& engine_id);

    // Reduce-only submission - bypasses position cap if reducing position
    // Returns true only if abs(next_pos) < abs(current_pos)
    bool submit_reduce_only(const std::string& client_id,
                            const std::string& symbol,
                            double price, double qty,
                            const std::string& engine_id);

    void poll();

    // Expose queue for external book updates (market feed wires here)
    QueuePositionModel& queue() { return ctx_->queue; }
    
    // Tier1+2 specific methods
    void start_tier1();  // Start lock-free router thread
    void stop_tier1();   // Stop router thread
    bool tier1_enabled() const { return tier1_enabled_; }
    
    // Tier2 revenue defense
    void init_symbol_tier2(const std::string& symbol, double base_cap, double latency_ms = 2.0);
    void on_pnl(const std::string& symbol, double pnl_dollars);
    void on_fill(const std::string& symbol, double signed_edge_bps);
    
    // Access to components
    // AtomicPositionGateAtomicPositionGate& gate() { gate() {  // Deprecated - use symbols_[] directly return gate_; }
    ElasticCapital& elastic() { return elastic_; }

private:
    std::shared_ptr<Context> ctx_;
    
    // Tier1 components
    bool tier1_enabled_{false};
    alignas(64) SymbolState symbols_[MAX_SYMBOLS];  // Tier 4/5: Fixed symbol IDs
    SignalRing<4096> ring_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    
    // Tier2 components
    ElasticCapital elastic_;
    FillToxicityFilter toxicity_;
    LatencyEVGate ev_gate_;
    MakerQueueHealth maker_health_;
    
    // Tier6-8 Hunt Mode Controls
    PositionGate position_gate_;
    DriftFilter drift_filter_;
    FundingEngine funding_engine_;
    VolatilityRegime vol_regime_[MAX_SYMBOLS];  // Per-symbol vol tracking
    SessionEngine session_engine_;
    ExecutionAlpha exec_alpha_;
    Microprice microprice_[MAX_SYMBOLS];  // Per-symbol microprice
    QueueModel queue_model_[MAX_SYMBOLS];  // Per-symbol queue model
    
    // Hunt mode state
    double last_mid_[MAX_SYMBOLS];  // For drift detection
    std::atomic<bool> hunt_mode_enabled_{true};  // Toggle for hunt mode
    
    // Profitability Components (Tier 0)
    AvellanedaStoikov as_pricer_;
    std::unordered_map<std::string, AdverseSelectionDetector> adverse_detectors_;
    std::unordered_map<std::string, ToxicityFilter> toxicity_filters_;
    
    // USD-Based Capital Management (Document 4)
    // Uses ctx_->profit for configuration
    USDPositionGate position_gate_usd_;
    SmartExecutor smart_executor_;
    
    // Tier1 router thread methods
    void run();
    void process(const TradeSignal& sig);
    void pin_core();
    void rate_limited_log(const std::string& msg);
    
    // Original members from ExecutionRouter
    ExecutionThrottle      throttle_;
    CancelReplaceCoalescer coalescer_;

    // ---------------------------------------------------------------------------
    // Atomic position gate — prevents race condition at position caps.
    // Lock held during position check to ensure no concurrent submissions
    // can violate position limits.
    // ---------------------------------------------------------------------------
    mutable std::mutex position_gate_mtx_;
    
    // ---------------------------------------------------------------------------
    // Negative Acknowledgement Backpressure (Cooldown System)
    // Prevents strategy spam when blocked by gates. Standard HFT pattern used
    // by NASDAQ OUCH, CME iLink, Binance VIP market makers.
    // ---------------------------------------------------------------------------
    struct Cooldown {
        uint64_t until_ns;
    };
    std::unordered_map<std::string, Cooldown> cooldowns_;
    mutable std::mutex cooldown_mtx_;
    uint64_t cooldown_duration_ns_{500'000'000};  // 500ms default
    
    // ---------------------------------------------------------------------------
    // Portfolio Edge Gate
    // Dynamic edge floor based on position utilization. Near cap = higher edge required.
    // Prevents cap-hovering churn and fee bleeding.
    // ---------------------------------------------------------------------------
    std::unordered_map<std::string, double> base_edge_bps_;
    double cap_k_{3.0};  // Utilization multiplier
    
    // Rate-limited logging to prevent log spam
    uint64_t last_log_ns_{0};
    uint64_t log_interval_ns_{5'000'000'000};  // 5 seconds between logs
    
    // Helper methods
    void set_cooldown(const std::string& engine_id, const std::string& symbol);
    bool check_cooldown(const std::string& engine_id, const std::string& symbol);
    void rate_log(const std::string& msg);
    uint64_t now_ns();

    // ---------------------------------------------------------------------------
    // Live execution state
    // ---------------------------------------------------------------------------
    // Hot path: WS Trading API for submit + cancel.
    BinanceWSExecution* ws_exec_{nullptr};

    // Cold path: REST client for cancel federation sweep only (fire-and-forget).
    BinanceRestClient* rest_client_{nullptr};

    // Orders submitted to exchange but not yet ACKed (or terminal).
    // Prevents duplicate REST submission on re-poll.
    // Cleared on ACK/FILL/CANCEL/REJECT (terminal OSM states).
    // Survived across WS outages — reconciled on reconnect.
    std::unordered_set<std::string> submitted_;

    // REST circuit breaker. Consecutive failures increment this counter.
    // At threshold: drift kill is triggered and no further submissions occur.
    // Reset to 0 on any successful REST call.
    static constexpr int CIRCUIT_BREAK_THRESHOLD = 3;
    int rest_failures_{0};

    // ---------------------------------------------------------------------------
    // Live path helpers — each runs exclusively on the CORE1 poll thread.
    // No additional locking needed (single consumer of coalescer in live mode).
    // ---------------------------------------------------------------------------

    // Submit a NEW order to Binance via WS Trading API. Adds to submitted_ on queue.
    // ws_exec_ is non-blocking (queues frame). Circuit breaker triggers on
    // WS disconnect, not on per-order failure.
    void live_submit(const std::string& client_id, const CoalesceOrder& ord);

    // Cancel an ACKED order by client ID via WS Trading API. Non-blocking.
    // Does NOT remove from coalescer — waits for CANCELED event on user stream.
    void live_cancel(const std::string& client_id, const OrderRecord& rec);

    // Reconcile in-flight orders against exchange truth after a WS reconnect.
    // Orders we submitted but exchange doesn't know about → REJECTED.
    // Orders on exchange we don't know about → phantom → hard kill.
    void live_reconcile();
};

} // namespace chimera
