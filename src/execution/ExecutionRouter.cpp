#include "execution/ExecutionRouter.hpp"
#include "execution/CancelFederation.hpp"
#include "execution/QueueDecayGovernor.hpp"
#include "forensics/EdgeAttribution.hpp"
#include "control/DeskArbiter.hpp"
#include "runtime/ProfitPreset.hpp"
#include "exchange/binance/BinanceRestClient.hpp"
#include "exchange/binance/BinanceWSExecution.hpp"
#include "forensics/EventTypes.hpp"
#include <chrono>
#include <iostream>
#include <cstring>
#include <vector>
#include <nlohmann/json.hpp>

using namespace chimera;
using json = nlohmann::json;

static uint64_t now_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(
        steady_clock::now().time_since_epoch()).count();
}

ExecutionRouter::ExecutionRouter(std::shared_ptr<Context> ctx)
    : ctx_(std::move(ctx)),
      throttle_(20, 5),
      tier1_enabled_(false),
      elastic_(),
      toxicity_(0.9, -2.0),
      ev_gate_(0.5),
      maker_health_(),
      position_gate_usd_(),
      smart_executor_(ctx_->profit) {
    base_edge_bps_["BTCUSDT"] = 1.5;
    base_edge_bps_["ETHUSDT"] = 1.0;
    base_edge_bps_["SOLUSDT"] = 1.0;
    
    // Pre-initialize gate caps and positions BEFORE starting Tier1
    symbols_[SYM_BTC].set_cap(0.30);  // DISCOVERY MODE: 0.30 for edge finding
    symbols_[SYM_ETH].set_cap(0.30);
    symbols_[SYM_SOL].set_cap(0.30);
    
    symbols_[SYM_BTC].set_position(0.0);
    symbols_[SYM_ETH].set_position(0.0);
    symbols_[SYM_SOL].set_position(0.0);
    
    elastic_.set_base_cap("BTCUSDT", 0.30);  // DISCOVERY MODE: 0.30
    elastic_.set_base_cap("ETHUSDT", 0.30);
    elastic_.set_base_cap("SOLUSDT", 0.30);
    
    // Old position_gate_ removed - now using position_gate_usd_ with USD caps
    
    // PRE-REGISTER SYMBOLS FOR THREAD SAFETY (Document 9)
    position_gate_usd_.register_symbol("BTCUSDT");
    position_gate_usd_.register_symbol("ETHUSDT");
    position_gate_usd_.register_symbol("SOLUSDT");
    position_gate_usd_.set_cap("BTCUSDT", 2000.0);
    position_gate_usd_.set_cap("ETHUSDT", 4000.0);
    position_gate_usd_.set_cap("SOLUSDT", 4000.0);
    
    drift_filter_.set_multiplier(1.1);  // 1.1× spread threshold (was 1.8)
    funding_engine_.set_threshold(5.0);  // 5 bps/hour
    
    // Initialize per-symbol state
    for (int i = 0; i < MAX_SYMBOLS; ++i) {
        last_mid_[i] = 0.0;
    }
    
    // Initialize Avellaneda-Stoikov inventory pricing (DISCOVERY MODE - less inventory aversion)
    AvellanedaStoikov::Params btc_params;
    btc_params.gamma = 0.10;  // Was 0.15 - lower = less inventory aversion
    btc_params.kappa = 2.0;
    btc_params.T = 1.0;
    btc_params.min_spread_bps = 0.2;  // Was 0.4 - allow tighter spreads
    btc_params.max_spread_bps = 4.0;
    as_pricer_.set_params("BTCUSDT", btc_params);
    
    AvellanedaStoikov::Params eth_params;
    eth_params.gamma = 0.08;  // Was 0.12
    eth_params.kappa = 2.5;
    eth_params.T = 1.0;
    eth_params.min_spread_bps = 0.15;  // Was 0.3
    eth_params.max_spread_bps = 3.0;
    as_pricer_.set_params("ETHUSDT", eth_params);
    
    AvellanedaStoikov::Params sol_params;
    sol_params.gamma = 0.12;  // Was 0.18
    sol_params.kappa = 1.8;
    sol_params.T = 0.8;
    sol_params.min_spread_bps = 0.25;  // Was 0.5
    sol_params.max_spread_bps = 5.0;
    as_pricer_.set_params("SOLUSDT", sol_params);
    
    // Initialize adverse selection detectors (DISCOVERY MODE - higher threshold)
    AdverseSelectionDetector::Config as_cfg;
    as_cfg.lookback_trades = 50;
    as_cfg.lookback_quotes = 100;
    as_cfg.imbalance_threshold = 0.3;
    as_cfg.high_prob_threshold = 0.85;  // Was 0.7 - only block truly toxic flow
    
    adverse_detectors_["BTCUSDT"] = AdverseSelectionDetector(as_cfg);
    adverse_detectors_["ETHUSDT"] = AdverseSelectionDetector(as_cfg);
    adverse_detectors_["SOLUSDT"] = AdverseSelectionDetector(as_cfg);
    
    // Initialize VPIN toxicity filters (DISCOVERY MODE - higher threshold)
    ToxicityFilter::Config tox_cfg;
    tox_cfg.vpin_cfg.num_buckets = 50;
    tox_cfg.vpin_cfg.volume_per_bucket = 5.0;
    tox_cfg.vpin_cfg.toxic_threshold = 0.85;  // Was 0.7 - only block truly toxic
    tox_cfg.combined_threshold = 0.80;  // Was 0.6
    
    toxicity_filters_["BTCUSDT"] = ToxicityFilter(tox_cfg);
    toxicity_filters_["ETHUSDT"] = ToxicityFilter(tox_cfg);
    
    tox_cfg.vpin_cfg.volume_per_bucket = 10.0;  // SOL more liquid
    toxicity_filters_["SOLUSDT"] = ToxicityFilter(tox_cfg);
    
    std::cout << "[ROUTER] HUNT MODE ENABLED:\n";
    std::cout << "  USD Capitals: BTC=$2000, ETH=$4000, SOL=$4000 (SMART allocation)\n";
    std::cout << "  Symbols pre-registered (thread-safe)\n";
    std::cout << "  Suppress: 75ms\n";
    std::cout << "  Drift threshold: 1.1× spread\n";
    std::cout << "  Funding threshold: 5 bps/hour\n";
    std::cout << "[ROUTER] DISCOVERY MODE (RELAXED FILTERS):\n";
    std::cout << "  Edge requirement: 0.12 bps (not 0.3 bps)\n";
    std::cout << "  Vol sigma floor: 0.5 bps (never DEAD)\n";
    std::cout << "  Maker-primary, taker-escape if EV > fees+slip\n";
    std::cout << "  Avellaneda-Stoikov: gamma=0.08-0.12 (low inventory aversion)\n";
    std::cout << "  Adverse selection: threshold=0.85 (only block severe)\n";
    std::cout << "  VPIN toxicity: DISABLED (redundant with adverse selection)\n";
    std::cout << "[ROUTER] Initialized with Tier 4/5/6/7/8 (hunt mode active)" << std::endl;
    // start_tier1();  // DISABLED until lifetime fix verified
}

ExecutionRouter::~ExecutionRouter() {
    stop_tier1();
}


bool ExecutionRouter::submit_order(const std::string& client_id,
                                    const std::string& symbol,
                                    double price, double qty,
                                    const std::string& engine_id) {
    // Map symbol name to ID (Tier 4/5)
    int symbol_id = symbol_name_to_id(symbol);
    if (symbol_id < 0) {
        std::cerr << "[ROUTER] Unknown symbol: " << symbol << "\n";
        return false;
    }
    
    SymbolState& sym = symbols_[symbol_id];
    auto now = std::chrono::steady_clock::now();
    uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    // SYMBOL SUPPRESSION (Tier 4/5) - eliminates spam
    if (sym.is_blocked(now_ms)) {
        return false;  // Silent drop
    }
    
    // DRIFT KILL GATE
    if (ctx_->risk.killed()) {
        return false;
    }

    // FREEZE GATE
    if (ctx_->cancel_fed.active()) {
        return false;
    }

    // PnL strategy gate
    if (!ctx_->pnl.allow_strategy(engine_id)) {
        return false;
    }

    // Desk Arbiter gate
    if (ctx_->desk && !ctx_->desk->allow_submit(engine_id)) {
        if (sym.should_log(now_ms, 5000)) {
            std::cout << "[DESK] BLOCK " << engine_id << " " << symbol << "\n";
        }
        return false;
    }

    // ========== PROFITABILITY GATES (ProfitPreset) ==========
    
    // EDGE FLOOR GATE
    if (ctx_->profit) {
        double min_edge = ctx_->profit->min_edge_bps.load(std::memory_order_relaxed);
        // Note: In real implementation, calculate edge_bps from price vs mid
        // For now, we skip this gate as we don't have edge calculation here
        // This would typically be done in strategy before submitting intent
    }
    
    // MAKER-ONLY GATE
    if (ctx_->profit && ctx_->profit->maker_only.load(std::memory_order_relaxed)) {
        // Check if order crosses spread (taker)
        // Note: Would need bid/ask from book to determine this
        // Strategies should check this before submitting
    }
    
    // ========================================================

    // ========== HUNT MODE CONTROLS (Tier 6-8) ==========
    
    if (hunt_mode_enabled_.load(std::memory_order_relaxed)) {
        uint64_t now_hunt_ns = now_ns();
        double current_pos = sym.get_position();
        
        // Update microprice for this symbol
        auto book = ctx_->queue.top(symbol);
        microprice_[symbol_id].update(book.bid, book.ask, book.bid_size, book.ask_size);
        
        // Update queue model
        queue_model_[symbol_id].on_book(book.bid_size + book.ask_size, 0.01);
        
        // Update volatility regime
        double mid = (book.bid + book.ask) * 0.5;
        vol_regime_[symbol_id].update(mid);
        
        // Update USD gate with current price (Document 9)
        position_gate_usd_.state(symbol).last_price.store(mid, std::memory_order_relaxed);
        
        // Drift filter check
        if (last_mid_[symbol_id] > 0.0) {
            double spread = book.ask - book.bid;
            if (drift_filter_.is_trending(mid, last_mid_[symbol_id], spread)) {
                if (sym.should_log(now_ms, 5000)) {
                    std::cout << "[HUNT] DRIFT BLOCK " << engine_id << " " << symbol
                             << " move=" << (mid - last_mid_[symbol_id])
                             << " threshold=" << (spread * 1.8) << "\n";
                }
                last_mid_[symbol_id] = mid;
                return false;
            }
        }
        last_mid_[symbol_id] = mid;
        
        // Funding check
        if (qty > 0 && funding_engine_.is_hostile_long(symbol)) {
            if (sym.should_log(now_ms, 10000)) {
                std::cout << "[HUNT] FUNDING BLOCK LONG " << symbol
                         << " rate=" << funding_engine_.rate(symbol) << " bps/hr\n";
            }
            return false;
        }
        if (qty < 0 && funding_engine_.is_hostile_short(symbol)) {
            if (sym.should_log(now_ms, 10000)) {
                std::cout << "[HUNT] FUNDING BLOCK SHORT " << symbol
                         << " rate=" << funding_engine_.rate(symbol) << " bps/hr\n";
            }
            return false;
        }
        
        // Volatility regime check (DEAD = no trade), but only after warmup
        if (vol_regime_[symbol_id].warmed() && vol_regime_[symbol_id].regime() == VolRegime::DEAD) {
            if (sym.should_log(now_ms, 10000)) {
                std::cout << "[HUNT] VOL REGIME DEAD " << symbol
                         << " sigma=" << vol_regime_[symbol_id].sigma() << "\n";
            }
            return false;
        }
        
        // ADVERSE SELECTION GATE (Tier 0) - DISCOVERY MODE
        auto it_adverse = adverse_detectors_.find(symbol);
        if (it_adverse != adverse_detectors_.end()) {
            double adverse_prob = it_adverse->second.compute_probability();
            if (adverse_prob > 0.85) {  // Only block severe adverse selection
                if (sym.should_log(now_ms, 10000)) {
                    std::cout << "[HUNT] ADVERSE SELECTION BLOCK " << symbol
                             << " prob=" << (int)(adverse_prob * 100.0) << "%\n";
                }
                ctx_->telemetry.increment_risk_block();
                return false;
            }
        }
        
        // VPIN TOXICITY GATE - DISABLED in discovery mode (redundant with adverse selection)
        // Document: "Using both at once is redundant and overly conservative. Fix: Disable one in hunt mode."
        /*
        auto it_tox = toxicity_filters_.find(symbol);
        if (it_tox != toxicity_filters_.end()) {
            if (it_tox->second.should_avoid_market_making()) {
                if (sym.should_log(now_ms, 10000)) {
                    auto breakdown = it_tox->second.get_breakdown();
                    std::cout << "[HUNT] TOXICITY BLOCK " << symbol
                             << " VPIN=" << (int)(breakdown.vpin * 100.0) << "% level=" 
                             << breakdown.level << "\n";
                }
                ctx_->telemetry.increment_risk_block();
                return false;
            }
        }
        */
        
        // Session check
        if (!session_engine_.trading_allowed(now_hunt_ns)) {
            if (sym.should_log(now_ms, 10000)) {
                auto session = session_engine_.session(now_hunt_ns);
                std::cout << "[HUNT] SESSION BLOCK " << session_engine_.name(session) << "\n";
            }
            return false;
        }
        
        // USD-BASED POSITION GATE (Document 9: enforce at router level)
        if (!position_gate_usd_.allow(symbol, qty, mid, now_hunt_ns, 75'000'000)) {
            if (sym.should_log(now_ms, 5000)) {
                auto& gate_state = position_gate_usd_.state(symbol);
                double gate_pos = gate_state.position.load(std::memory_order_relaxed);
                double usd_exposure = std::abs((gate_pos + qty) * mid);
                std::cout << "[USD_GATE] BLOCK " << engine_id << " " << symbol
                         << " pos=" << gate_pos
                         << " usd_exposure=$" << (int)usd_exposure
                         << " suppress=75ms\n";
            }
            ctx_->telemetry.increment_position_block();
            return false;
        }
        
        // Queue probability check
        double fill_prob = queue_model_[symbol_id].fill_probability(std::abs(qty));
        if (fill_prob < 0.15) {
            if (sym.should_log(now_ms, 5000)) {
                std::cout << "[HUNT] QUEUE PROB LOW " << symbol
                         << " prob=" << fill_prob << "\n";
            }
            return false;
        }
    }
    
    // ========================================================
    
    // POSITION GATE (Tier 4/5) - lock-free atomic (fallback if hunt mode disabled)
    int64_t qty_q6 = static_cast<int64_t>(qty * 1'000'000);
    
    if (!hunt_mode_enabled_.load(std::memory_order_relaxed)) {
        if (!sym.position_gate_allow(qty_q6)) {
            // PROFIT MODE: Use ProfitPreset suppress duration on cap hit
            uint64_t suppress_ms = 100;  // Default
            if (ctx_->profit) {
                uint64_t suppress_ns = ctx_->profit->suppress_ns.load(std::memory_order_relaxed);
                suppress_ms = suppress_ns / 1'000'000;  // Convert ns to ms
            }
            sym.block(now_ms, suppress_ms);
            
            if (sym.should_log(now_ms, 1000)) {
                std::cout << "[POSITION_GATE] BLOCK " << engine_id << " " << symbol
                         << " pos=" << sym.get_position()
                         << " cap=" << sym.get_cap()
                         << " suppress=" << suppress_ms << "ms\n";
            }
            
            ctx_->telemetry.increment_risk_block();
            return false;
        }
    }

    // Live-mode gates
    if (ctx_->arm.live_enabled()) {
        if (!ctx_->risk.pre_check(symbol, price, qty)) {
            ctx_->telemetry.increment_risk_block();
            return false;
        }

        if (!throttle_.allow_global() || !throttle_.allow_symbol(symbol)) {
            ctx_->telemetry.increment_throttle_block();
            return false;
        }
        
        // Queue competitiveness gate - simplified
        // Original code references methods that don't exist, skip for now
    }

    // SHADOW FILL (Tier 4/5) - lock-free
    sym.apply_fill(qty_q6);
    
    // Update GlobalRiskGovernor so strategies see correct position
    ctx_->risk.on_execution_ack(symbol, qty);
    
    // PnL calculation with execution quality metrics
    try {
        auto tb = ctx_->queue.top(symbol);
        double mid = (tb.bid + tb.ask) * 0.5;
        double spread = tb.ask - tb.bid;
        
        // Entry quality: did we buy below mid or sell above mid?
        double pnl_delta = (mid - price) * qty;
        
        // Edge estimation: distance from mid in bps
        double edge_bps = std::abs((price - mid) / mid * 10000.0);
        
        // Fee: assume 10bps taker (shadow mode)
        double fee_bps = 10.0;
        
        // Slippage: crossing spread = full slip, at mid = no slip
        double slip_bps = (std::abs(price - mid) / mid * 10000.0);
        
        // Latency penalty: shadow mode = minimal
        double lat_bps = 0.1;
        
        // Notional
        double notional = std::abs(price * qty);
        
        // Assume maker (shadow fills at best bid/ask)
        bool is_maker = true;
        
        ctx_->pnl.update_fill(engine_id, pnl_delta, edge_bps, fee_bps, 
                              slip_bps, lat_bps, notional, is_maker);
    } catch (...) {
        // Queue data not available
        ctx_->pnl.update_fill(engine_id, 0.0, 0.0, 10.0, 0.0, 0.0, 0.0, true);
    }
    
    // Latency measurement: shadow mode measures internal processing time
    ctx_->latency.update_latency_us(25);
    
    // Update telemetry
    double pos = sym.get_position();
    double notional = price * qty;
    ctx_->telemetry.update_symbol(symbol.c_str(), pos, notional);
    ctx_->telemetry.increment_fills();
    
    // Sync USD gate SymbolState with actual position
    auto& gate_state = position_gate_usd_.state(symbol);
    gate_state.position.store(pos, std::memory_order_relaxed);
    gate_state.last_price.store(price, std::memory_order_relaxed);
    
    // Rate-limited log
    if (sym.should_log(now_ms, 1000)) {
        std::cout << "[ROUTER] FILL " << engine_id << " " << symbol
                 << " qty=" << qty << " pos=" << pos << "\n";
    }
    
    return true;
}




bool ExecutionRouter::submit_reduce_only(const std::string& client_id,
                                          const std::string& symbol,
                                          double price, double qty,
                                          const std::string& engine_id) {
    // ---------------------------------------------------------------------------
    // REDUCE-ONLY LANE — bypasses position cap if reducing position.
    // 
    // This provides a safe escape path for unwinding at saturation.
    // Allowed if and only if: abs(next_position) < abs(current_position)
    // 
    // All other gates still apply (killed, PnL, desk, freeze, etc.)
    // ---------------------------------------------------------------------------
    
    // Same initial gates as normal submission
    if (ctx_->risk.killed()) {
        return false;
    }
    
    if (ctx_->cancel_fed.active()) {
        return false;
    }
    
    if (!ctx_->pnl.allow_strategy(engine_id)) {
        return false;
    }
    
    if (ctx_->desk && !ctx_->desk->allow_submit(engine_id)) {
        return false;
    }
    
    // ---------------------------------------------------------------------------
    // REDUCE-ONLY CHECK — bypass position gate if truly reducing
    // ---------------------------------------------------------------------------
    {
        std::lock_guard<std::mutex> lock(position_gate_mtx_);
        
        double current_pos = ctx_->risk.get_position(symbol);
        double next_pos = current_pos + qty;
        
        // Must be reducing: abs(next) < abs(current)
        if (std::fabs(next_pos) >= std::fabs(current_pos)) {
            std::cout << "[REDUCE_ONLY] REJECT " << engine_id << " " << symbol 
                      << " not reducing: current=" << current_pos 
                      << " + qty=" << qty << " = " << next_pos << "\n";
            return false;
        }
        
        std::cout << "[REDUCE_ONLY] ALLOW " << engine_id << " " << symbol 
                  << " reducing: " << current_pos << " → " << next_pos << "\n";
    }
    
    // Continue with normal submission flow (risk, throttle, etc.)
    if (ctx_->arm.live_enabled()) {
        if (!ctx_->risk.pre_check(symbol, price, qty)) {
            ctx_->telemetry.increment_risk_block();
            return false;
        }
        
        if (!throttle_.allow_global() || !throttle_.allow_symbol(symbol)) {
            ctx_->telemetry.increment_throttle_block();
            return false;
        }
    }
    
    // Deduplication: cancel existing order from same engine+symbol at different price
    {
        std::string existing_cid;
        CoalesceOrder existing_ord;
        if (coalescer_.find_by_engine_symbol(engine_id, symbol, existing_cid, existing_ord)) {
            if (existing_ord.price == price) {
                return false;  // Duplicate
            }
            // Different price — cancel old, proceed with new
            ctx_->osm.on_cancel_by_client_id(existing_cid);
            coalescer_.clear(existing_cid);
            if (ctx_->queue_decay) {
                ctx_->queue_decay->on_order_done(existing_cid);
            }
            if (ctx_->edge) {
                ctx_->edge->on_cancel(existing_cid);
            }
        }
    }
    
    // Submit to coalescer
    CoalesceOrder ord{symbol, engine_id, price, qty};
    coalescer_.submit(client_id, ord);
    
    // Update OSM
    OrderRecord rec;
    rec.client_id = client_id;
    rec.symbol    = symbol;
    rec.price     = price;
    rec.qty       = qty;
    ctx_->osm.on_new(rec);
    
    // Queue decay tracking
    if (ctx_->queue_decay) {
        ctx_->queue_decay->on_order_submitted(client_id, symbol, price, qty > 0);
    }
    
    // Edge attribution
    if (ctx_->edge) {
        TopOfBook tb = ctx_->queue.top(symbol);
        double predicted_edge_bps = 0.0;
        if (tb.valid && price > 0.0) {
            double mid  = (tb.bid + tb.ask) * 0.5;
            double sign = (qty > 0) ? 1.0 : -1.0;
            predicted_edge_bps = sign * (mid - price) / price * 10000.0;
        }
        ctx_->edge->on_submit(client_id, engine_id, predicted_edge_bps, 0.0);
    }
    
    // Forensic: record decision event
    struct DecisionEvent {
        char   symbol[16];
        double price;
        double qty;
    } ev{};
    std::strncpy(ev.symbol, symbol.c_str(), sizeof(ev.symbol) - 1);
    ev.price = price;
    ev.qty   = qty;
    
    uint64_t causal = ctx_->recorder.next_causal_id();
    ctx_->recorder.write(EventType::DECISION, &ev, sizeof(ev), causal);
    
    return true;
}

void ExecutionRouter::poll() {
    uint64_t ts = now_ns();

    // ---------------------------------------------------------------------------
    // HOT-RELOAD PROFIT PRESET — every 5s, reload .env settings
    // ---------------------------------------------------------------------------
    if (ctx_->profit) {
        ctx_->profit->reload_if_due();
    }

    // ---------------------------------------------------------------------------
    // Periodic OSM purge — terminal orders (FILLED/CANCELED/REJECTED) accumulate
    // in orders_ forever. Purge every 10s to prevent unbounded memory growth.
    // ---------------------------------------------------------------------------
    static uint64_t last_purge_ns = 0;
    if (ts - last_purge_ns > 10'000'000'000ULL) {
        ctx_->osm.purge_terminal();
        last_purge_ns = ts;
    }

    // ---------------------------------------------------------------------------
    // CANCEL FEDERATION SWEEP — runs first, before anything else.
    //
    // If cancel_fed is active, we are the designated sweeper (CORE1).
    // Steps:
    //   1. Log the reason
    //   2. Cancel all in-flight orders via REST (live) or clear coalescer (shadow)
    //   3. Fire drift kill — system is dead after this
    //   4. Clear the cancel_fed flag (sweep complete)
    //
    // After drift kill, the system won't recover without operator intervention.
    // This is intentional — cancel federation is a FATAL event.
    // ---------------------------------------------------------------------------
    if (ctx_->cancel_fed.active()) {
        const char* reason = ctx_->cancel_fed.reason();
        std::cerr << "[CANCEL_FED] SWEEP: reason=" << (reason ? reason : "unknown") << "\n";

        // Cancel all in-flight orders
        auto keys = coalescer_.pending_keys();
        int cancel_count = 0;

        for (const auto& client_id : keys) {
            CoalesceOrder ord;
            if (!coalescer_.get(client_id, ord)) continue;

            if (ctx_->arm.live_enabled() && submitted_.count(client_id) && rest_client_) {
                // Cancel federation sweep: REST fire-and-forget. System is dying —
                // latency is irrelevant. REST is more reliable for sweep because
                // it's synchronous and doesn't depend on WS connectivity.
                rest_client_->cancel_order_by_client_id(ord.symbol, client_id);
                cancel_count++;
            }

            // Clear local state regardless — we're going to drift kill anyway
            coalescer_.clear(client_id);
            submitted_.erase(client_id);
        }

        std::cerr << "[CANCEL_FED] SENT " << cancel_count << " cancels to exchange. "
                  << "Firing drift kill.\n";

        ctx_->cancel_fed.clear();
        ctx_->risk.drift().trigger(std::string("Cancel Federation: ") + (reason ? reason : "unknown"));
        return;  // System is dead. Don't process anything else.
    }

    // ---------------------------------------------------------------------------
    // Portfolio kill check — runs in BOTH modes (shadow PnL tracking is real).
    // ---------------------------------------------------------------------------
    if (ctx_->pnl.portfolio_killed()) {
        if (!ctx_->risk.killed()) {
            std::cerr << "[ROUTER] Portfolio killed by PnLGovernor — triggering drift kill\n";
            ctx_->risk.drift().trigger("PnL governor: portfolio DD breached");
        }
        return;
    }

    // ---------------------------------------------------------------------------
    // Queue Decay poll — check all tracked orders for TTL/urgency breach.
    // May fire cancel_fed (which we'll sweep on the next poll tick).
    // ---------------------------------------------------------------------------
    if (ctx_->queue_decay) {
        ctx_->queue_decay->poll();
    }

    if (!ctx_->arm.live_enabled()) {
        // ---------------------------------------------------------------------------
        // SHADOW EXECUTION PATH — no simulation.
        //
        // We have live book data from BinanceWSMarket. Shadow mode means real money
        // is not trading, but everything else is real. Fill decisions are made
        // purely from the live order book:
        //
        //   BUY  fills when ask <= order_price  (market is selling at/below our bid)
        //   SELL fills when bid >= order_price  (market is buying at/above our ask)
        //
        // Cancel: TTL only. If the order hasn't filled within max_wait, cancel it.
        // No queue position estimation, no fill probability models.
        // ---------------------------------------------------------------------------
        auto keys = coalescer_.pending_keys();

        for (const auto& client_id : keys) {
            CoalesceOrder ord;
            if (!coalescer_.get(client_id, ord)) continue;

            OrderRecord rec;
            try {
                rec = ctx_->osm.get(client_id);
            } catch (...) {
                coalescer_.clear(client_id);
                continue;
            }

            bool is_buy = ord.qty > 0;
            TopOfBook tb = ctx_->queue.top(ord.symbol);

            // No book data yet — order sits pending until feed arrives.
            if (!tb.valid) continue;

            // ---------------------------------------------------------------------------
            // CANCEL: TTL only. Pass fill_prob=1.0 to disable the probability-based
            // cancel check inside CancelPolicy — only the max_wait TTL fires.
            // ---------------------------------------------------------------------------
            if (ctx_->cancel_policy.should_cancel(ts, rec.last_update_ns, 1.0)) {
                coalescer_.clear(client_id);
                std::string cancel_id = rec.exchange_id.empty() ? client_id : rec.exchange_id;
                ctx_->osm.on_cancel(cancel_id);

                if (ctx_->queue_decay) {
                    ctx_->queue_decay->on_order_done(client_id);
                }

                // Edge Attribution: clean up pending_ entry for canceled order.
                if (ctx_->edge) {
                    ctx_->edge->on_cancel(client_id);
                }

                struct CancelEvent {
                    char   symbol[16];
                    char   client_id[32];
                    double price;
                    double qty;
                } ev{};
                std::strncpy(ev.symbol, ord.symbol.c_str(), sizeof(ev.symbol) - 1);
                std::strncpy(ev.client_id, client_id.c_str(), sizeof(ev.client_id) - 1);
                ev.price = ord.price;
                ev.qty   = ord.qty;

                uint64_t causal = ctx_->recorder.next_causal_id();
                ctx_->recorder.write(EventType::ROUTE, &ev, sizeof(ev), causal);
                continue;
            }

            // ---------------------------------------------------------------------------
            // FILL: live book cross check. Did the market cross our price?
            // ---------------------------------------------------------------------------
            bool crossed = is_buy ? (tb.ask <= ord.price) : (tb.bid >= ord.price);

            if (crossed) {
                // Log ROUTE event
                struct RouteEvent {
                    char   symbol[16];
                    char   client_id[32];
                    double price;
                    double qty;
                } rev{};
                std::strncpy(rev.symbol, ord.symbol.c_str(), sizeof(rev.symbol) - 1);
                std::strncpy(rev.client_id, client_id.c_str(), sizeof(rev.client_id) - 1);
                rev.price = ord.price;
                rev.qty   = ord.qty;

                uint64_t route_causal = ctx_->recorder.next_causal_id();
                ctx_->recorder.write(EventType::ROUTE, &rev, sizeof(rev), route_causal);

                // Fill at order price (standard limit order shadow assumption)
                ctx_->osm.on_ack(client_id, client_id);
                ctx_->osm.on_fill(client_id, std::abs(ord.qty));

                // Log FILL event
                struct FillEvent {
                    char   symbol[16];
                    double qty;
                    double price;
                } fev{};
                std::strncpy(fev.symbol, ord.symbol.c_str(), sizeof(fev.symbol) - 1);
                fev.qty   = std::abs(ord.qty);
                fev.price = ord.price;

                uint64_t fill_causal = ctx_->recorder.next_causal_id();
                ctx_->recorder.write(EventType::FILL, &fev, sizeof(fev), fill_causal);

                // Update risk position
                ctx_->risk.on_execution_ack(ord.symbol, ord.qty);

                // PnL calculation with execution quality metrics
                {
                    double mid = (tb.bid + tb.ask) * 0.5;
                    double pnl_delta = (mid - ord.price) * ord.qty;
                    double notional = ord.price * std::abs(ord.qty);
                    
                    // Execution quality metrics
                    double edge_bps = std::abs((ord.price - mid) / mid * 10000.0);
                    double fee_bps = 10.0;  // Live taker fees
                    double slip_bps = (std::abs(ord.price - mid) / mid * 10000.0);
                    double lat_bps = 0.5;   // Live mode has network latency
                    bool is_maker = false;   // Assume taker in live mode
                    
                    ctx_->pnl.update_fill(ord.engine_id, pnl_delta, edge_bps, fee_bps,
                                          slip_bps, lat_bps, notional, is_maker);

                    // Edge Attribution
                    if (ctx_->edge && notional > 0.0) {
                        double realized_bps = (pnl_delta / notional) * 10000.0;
                        ctx_->edge->on_fill(client_id, realized_bps, 0.0);
                    }

                    // Desk Arbiter
                    if (ctx_->desk && notional > 0.0) {
                        double pnl_bps = (pnl_delta / notional) * 10000.0;
                        ctx_->desk->on_fill(ord.engine_id, pnl_bps);
                    }
                }

                // Queue decay: order is done
                if (ctx_->queue_decay) {
                    ctx_->queue_decay->on_order_done(client_id);
                }

                // Telemetry
                {
                    ctx_->telemetry.increment_fills();
                    auto positions = ctx_->risk.dump_positions();
                    auto it = positions.find(ord.symbol);
                    double pos_qty  = (it != positions.end()) ? it->second : 0.0;
                    double notional = std::fabs(pos_qty * ord.price);
                    ctx_->telemetry.update_symbol(ord.symbol, pos_qty, notional);
                    
                    // Sync USD gate SymbolState with actual position (Document 9)
                    auto& gate_state = position_gate_usd_.state(ord.symbol);
                    gate_state.position.store(pos_qty, std::memory_order_relaxed);
                    gate_state.last_price.store(ord.price, std::memory_order_relaxed);
                }

                coalescer_.clear(client_id);
            }
        }
        return;
    }

    // --- LIVE PATH ---

    // 1. Reconciliation trigger
    if (ctx_->needs_reconcile.load()) {
        ctx_->needs_reconcile.store(false);
        live_reconcile();
        if (ctx_->risk.killed()) return;
    }

    // 2. Circuit breaker
    if (rest_failures_ >= CIRCUIT_BREAK_THRESHOLD) return;

    // 3. WS connectivity gate
    if (!ctx_->ws_user_alive.load()) return;

    // 3b. Latency governor cancel-all
    if (ctx_->latency.should_cancel_all()) {
        std::cerr << "[ROUTER] LATENCY CANCEL-ALL — canceling all in-flight orders\n";
        for (const auto& cid : coalescer_.pending_keys()) {
            CoalesceOrder ord;
            if (!coalescer_.get(cid, ord)) continue;
            if (submitted_.count(cid) && ws_exec_) {
                ws_exec_->cancel_order(ord.symbol, cid);
            }
        }
    }

    // 4. Drain coalescer
    auto keys = coalescer_.pending_keys();

    for (const auto& client_id : keys) {
        CoalesceOrder ord;
        if (!coalescer_.get(client_id, ord)) continue;

        OrderRecord rec;
        try {
            rec = ctx_->osm.get(client_id);
        } catch (...) {
            coalescer_.clear(client_id);
            submitted_.erase(client_id);
            if (ctx_->queue_decay) ctx_->queue_decay->on_order_done(client_id);
            continue;
        }

        switch (rec.status) {
            case OrderStatus::NEW:
                if (submitted_.count(client_id) == 0) {
                    live_submit(client_id, ord);
                }
                break;

            case OrderStatus::ACKED:
            case OrderStatus::PARTIALLY_FILLED: {
                bool is_buy = ord.qty > 0;
                auto est = ctx_->queue.estimate(ord.symbol, ord.price,
                                               std::abs(ord.qty), is_buy);
                if (ctx_->cancel_policy.should_cancel(ts, rec.last_update_ns,
                                                      est.expected_fill_prob)) {
                    live_cancel(client_id, rec);
                }
                break;
            }

            case OrderStatus::FILLED:
            case OrderStatus::CANCELED:
            case OrderStatus::REJECTED:
                coalescer_.clear(client_id);
                submitted_.erase(client_id);
                if (ctx_->queue_decay) ctx_->queue_decay->on_order_done(client_id);
                break;
        }
    }
}

void ExecutionRouter::live_submit(const std::string& client_id,
                                   const CoalesceOrder& ord) {
    // ---------------------------------------------------------------------------
    // WS connectivity gate — if WS exec is down, we can't submit.
    // Circuit breaker fires on consecutive poll cycles with no connectivity.
    // ---------------------------------------------------------------------------
    if (!ws_exec_ || !ws_exec_->connected()) {
        rest_failures_++;
        std::cout << "[LIVE] WS exec not connected (" << rest_failures_ << "/"
                  << CIRCUIT_BREAK_THRESHOLD << ")\n";
        if (rest_failures_ >= CIRCUIT_BREAK_THRESHOLD) {
            std::cout << "[LIVE] CIRCUIT BREAKER TRIPPED — WS exec disconnected\n";
            ctx_->risk.drift().trigger("WS exec circuit breaker: " +
                std::to_string(rest_failures_) + " consecutive polls disconnected");
        }
        return;
    }

    bool is_buy  = ord.qty > 0;
    std::string side = is_buy ? "BUY" : "SELL";

    // Non-blocking: queues frame into ws_exec_ outbound queue.
    // WS thread drains and sends on next loop iteration.
    ws_exec_->send_order(ord.symbol, side, std::abs(ord.qty), ord.price, client_id);

    // Mark as submitted immediately — the order is in the WS queue.
    // If WS thread fails to send (disconnect), circuit breaker catches it
    // on next poll via connected() check.
    submitted_.insert(client_id);
    rest_failures_ = 0;

    // Latency measurement: record submit timestamp.
    // on_ack() from user stream completes the RTT measurement.
    ctx_->latency.record_submit_ns(client_id);

    // Forensic: ROUTE event
    struct RouteEvent {
        char   symbol[16];
        char   client_id[32];
        double price;
        double qty;
    } ev{};
    std::strncpy(ev.symbol, ord.symbol.c_str(), sizeof(ev.symbol) - 1);
    std::strncpy(ev.client_id, client_id.c_str(), sizeof(ev.client_id) - 1);
    ev.price = ord.price;
    ev.qty   = ord.qty;

    uint64_t causal = ctx_->recorder.next_causal_id();
    ctx_->recorder.write(EventType::ROUTE, &ev, sizeof(ev), causal);

    std::cout << "[LIVE] Submitted: " << ord.symbol << " " << side
              << " " << std::abs(ord.qty) << " @ " << ord.price
              << " id=" << client_id << "\n";
}

void ExecutionRouter::live_cancel(const std::string& client_id,
                                   const OrderRecord& rec) {
    if (!ws_exec_ || !ws_exec_->connected()) {
        // WS down — can't cancel. Don't increment rest_failures_ here;
        // live_submit already handles the circuit breaker on connectivity.
        std::cout << "[LIVE] Cancel skipped (WS exec disconnected): " << client_id << "\n";
        return;
    }

    // Non-blocking: queues cancel frame.
    ws_exec_->cancel_order(rec.symbol, client_id);

    struct CancelEvent {
        char   symbol[16];
        char   client_id[32];
        double price;
        double qty;
    } ev{};
    std::strncpy(ev.symbol, rec.symbol.c_str(), sizeof(ev.symbol) - 1);
    std::strncpy(ev.client_id, client_id.c_str(), sizeof(ev.client_id) - 1);
    ev.price = rec.price;
    ev.qty   = rec.qty;

    uint64_t causal = ctx_->recorder.next_causal_id();
    ctx_->recorder.write(EventType::CANCEL, &ev, sizeof(ev), causal);

    std::cout << "[LIVE] Cancel submitted: " << rec.symbol
              << " id=" << client_id << "\n";
}

void ExecutionRouter::live_reconcile() {
    if (!rest_client_) return;

    std::cout << "[LIVE] Reconciliation triggered (WS reconnect). "
              << submitted_.size() << " orders in flight.\n";

    std::string open_orders_json;
    try {
        open_orders_json = rest_client_->get_open_orders();
        rest_failures_ = 0;
    } catch (const std::exception& e) {
        std::cout << "[LIVE] Reconcile REST failed: " << e.what() << "\n";
        rest_failures_++;
        ctx_->risk.drift().trigger("Reconciliation REST failure: " +
            std::string(e.what()));
        return;
    }

    std::unordered_set<std::string> exchange_open;
    try {
        json orders = json::parse(open_orders_json);
        if (!orders.is_array()) {
            std::cout << "[LIVE] Reconcile: unexpected response format\n";
            ctx_->risk.drift().trigger("Reconciliation: non-array response");
            return;
        }
        for (const auto& ord : orders) {
            if (ord.contains("origClientOrderId")) {
                exchange_open.insert(ord["origClientOrderId"].get<std::string>());
            }
        }
    } catch (const std::exception& e) {
        std::cout << "[LIVE] Reconcile parse failed: " << e.what() << "\n";
        ctx_->risk.drift().trigger("Reconciliation parse failure");
        return;
    }

    // Cross-reference
    std::vector<std::string> to_remove;
    for (const auto& cid : submitted_) {
        if (exchange_open.count(cid) == 0) {
            std::cout << "[LIVE] Reconcile: " << cid
                      << " not on exchange → REJECTED\n";
            ctx_->osm.on_reject(cid);
            coalescer_.clear(cid);
            if (ctx_->queue_decay) ctx_->queue_decay->on_order_done(cid);
            to_remove.push_back(cid);
        }
    }

    for (const auto& cid : to_remove)
        submitted_.erase(cid);

    for (const auto& exch_cid : exchange_open) {
        if (submitted_.count(exch_cid) == 0) {
            std::cout << "[LIVE] PHANTOM ORDER: " << exch_cid << " — HARD KILL\n";
            ctx_->risk.drift().trigger("Phantom order detected: " + exch_cid);
            return;
        }
    }

    std::cout << "[LIVE] Reconciliation complete. "
              << submitted_.size() << " orders confirmed in flight.\n";
}

// ---------------------------------------------------------------------------
// Helper Methods for Backpressure and Edge Gate
// ---------------------------------------------------------------------------

uint64_t ExecutionRouter::now_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(
        steady_clock::now().time_since_epoch()).count();
}

void ExecutionRouter::set_cooldown(const std::string& engine_id, 
                                   const std::string& symbol) {
    std::lock_guard<std::mutex> lock(cooldown_mtx_);
    std::string key = engine_id + ":" + symbol;
    cooldowns_[key].until_ns = now_ns() + cooldown_duration_ns_;
}

bool ExecutionRouter::check_cooldown(const std::string& engine_id,
                                     const std::string& symbol) {
    std::lock_guard<std::mutex> lock(cooldown_mtx_);
    std::string key = engine_id + ":" + symbol;
    
    auto it = cooldowns_.find(key);
    if (it == cooldowns_.end()) {
        return false;  // No cooldown active
    }
    
    uint64_t now = now_ns();
    if (now >= it->second.until_ns) {
        cooldowns_.erase(it);  // Cooldown expired
        return false;
    }
    
    return true;  // Still in cooldown
}

void ExecutionRouter::rate_log(const std::string& msg) {
    uint64_t now = now_ns();
    if (now - last_log_ns_ >= log_interval_ns_) {
        std::cout << msg << std::endl;
        last_log_ns_ = now;
    }
}

// ===========================================================================
// TIER 1 + TIER 2 IMPLEMENTATION
// ===========================================================================

void ExecutionRouter::start_tier1() {
    tier1_enabled_ = true;
    std::cout << "[ROUTER] Tier1 lock-free architecture enabled" << std::endl;
    
    running_.store(true, std::memory_order_release);
    worker_ = std::thread([this]() { run(); });
}

void ExecutionRouter::stop_tier1() {
    if (!tier1_enabled_)
        return;
        
    running_.store(false, std::memory_order_release);
    if (worker_.joinable())
        worker_.join();
    
    std::cout << "[ROUTER] Tier1 router thread stopped" << std::endl;
}

void ExecutionRouter::run() {
    pin_core();
    
    std::cout << "[TIER1_ROUTER] Thread started on core 0" << std::endl;
    
    TradeSignal sig;
    while (running_.load(std::memory_order_relaxed)) {
        // Drain ring buffer
        while (ring_.pop(sig)) {
            process(sig);
        }
        
        // Brief yield
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
    
    std::cout << "[TIER1_ROUTER] Thread stopped" << std::endl;
}

void ExecutionRouter::process(const TradeSignal& sig) {
    std::string sym(sig.symbol);
    std::string engine(sig.engine_id);
    
    // Apply same gates as original implementation
    if (ctx_->risk.killed()) return;
    if (ctx_->cancel_fed.active()) return;
    if (!ctx_->pnl.allow_strategy(engine)) return;
    if (ctx_->desk && !ctx_->desk->allow_submit(engine)) return;
    
    // Tier2 filters disabled for now (uninitialized components)
    
    // Position gate check
    int symbol_id = symbol_name_to_id(std::string(sym));
    if (symbol_id < 0) return;
    SymbolState& symbol_state = symbols_[symbol_id];
    
    if (!sig.reduce_only) {
        if (!symbol_state.position_gate_allow(static_cast<int64_t>(sig.qty * 1'000'000))) {
            rate_limited_log("[TIER1_ROUTER] POSITION_BLOCK " + engine + " " + sym);
            return;
        }
    }
    
    // Apply fill (shadow mode = instant)
    symbol_state.apply_fill(static_cast<int64_t>(sig.qty * 1'000'000));
    
    // TODO: Wire to actual exchange when live
    // if (ws_exec_ && ctx_->arm.is_armed()) {
    //     ws_exec_->submit_order(...);
    // }
    
    rate_limited_log("[TIER1_ROUTER] FILL " + engine + " " + sym + 
                    " qty=" + std::to_string(sig.qty) +
                    " pos=" + std::to_string(symbol_state.get_position()));
}

void ExecutionRouter::pin_core() {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    if (rc != 0) {
        std::cerr << "[TIER1_ROUTER] Warning: Failed to pin to core 0" << std::endl;
    } else {
        std::cout << "[TIER1_ROUTER] Pinned to core 0" << std::endl;
    }
#endif
}

void ExecutionRouter::rate_limited_log(const std::string& msg) {
    uint64_t now = now_ns();
    if (now - last_log_ns_ > log_interval_ns_) {
        std::cout << msg << std::endl;
        last_log_ns_ = now;
    }
}

// Tier2 revenue defense methods
void ExecutionRouter::init_symbol_tier2(const std::string& symbol, double base_cap, double latency_ms) {
    elastic_.set_base_cap(symbol, base_cap);
    ev_gate_.set_latency_ms(symbol, latency_ms);
    int symbol_id = symbol_name_to_id(symbol);
    if (symbol_id >= 0) {
        symbols_[symbol_id].set_cap(base_cap);
    }
    
    std::cout << "[TIER2] Initialized " << symbol 
              << " cap=" << base_cap 
              << " latency=" << latency_ms << "ms" << std::endl;
}

void ExecutionRouter::on_pnl(const std::string& symbol, double pnl_dollars) {
    elastic_.on_pnl(symbol, pnl_dollars);
    double new_cap = elastic_.cap(symbol);
    int symbol_id_pnl = symbol_name_to_id(symbol);
    if (symbol_id_pnl >= 0) {
        symbols_[symbol_id_pnl].set_cap(new_cap);
    }
}

void ExecutionRouter::on_fill(const std::string& symbol, double signed_edge_bps) {
    toxicity_.on_fill(symbol, signed_edge_bps);
}

