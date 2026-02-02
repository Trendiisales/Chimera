#include <csignal>
#include <thread>
#include <iostream>
#include <iomanip>
#include <memory>
#include <atomic>
#include <cstring>
#include <fstream>
#include <sstream>

#include "runtime/Context.hpp"
#include "runtime/ContextSnapshotter.hpp"
#include "runtime/ColdStartReconciler.hpp"
#include "runtime/ThreadModel.hpp"

#include "telemetry/HttpServer.hpp"
#include "exchange/binance/BinanceWSMarket.hpp"
#include "exchange/binance/BinanceWSUser.hpp"
#include "exchange/binance/BinanceWSExecution.hpp"
#include "exchange/binance/BinanceReconciler.hpp"

#include "execution/ExecutionRouter.hpp"

#include "runtime/ExchangeTruthLoop.hpp"
#include "execution/QueueDecayGovernor.hpp"
#include "forensics/EdgeAttribution.hpp"
#include "control/DeskArbiter.hpp"
#include "control/UnwindCoordinator.hpp"

#include "strategy/StrategyContext.hpp"
#include "strategy/StrategyRunner.hpp"
#include "strategy/BTCascade.hpp"
#include "strategy/ETHSniper.hpp"
#include "strategy/MeanReversion.hpp"
#include "strategy/QueueMarketMaker.hpp"
#include "strategy/ImpulseReversion.hpp"
#include "strategy/PortfolioSkewTrader.hpp"
#include "strategy/ETHFade.hpp"
#include "strategy/SOLFade.hpp"

#include <curl/curl.h>

using namespace chimera;

// ---------------------------------------------------------------------------
// Global UnwindCoordinator — prevents engines from fighting at position caps.
// All engines access this singleton to coordinate position limit behavior.
// Must be defined in chimera namespace to match extern declarations in engines.
// ---------------------------------------------------------------------------
namespace chimera {
    UnwindCoordinator g_unwind_coordinator;
}

// ---------------------------------------------------------------------------
// FIX 1.2: Signal-safe shutdown flag.
// Previously: handle_sigint() called g_snap->save() and used g_ctx->running.store().
// Both are unsafe in signal context:
//   - save() uses iostream, mutex, file I/O — all async-signal-unsafe.
//   - running.store() on atomic<bool> IS safe, but the save() was not.
//
// Now: signal handler ONLY sets the atomic flag. All cleanup (including snapshot
// save) happens in the main thread's shutdown sequence after the run loops exit.
// The flag is a plain atomic<bool> — the only thing the signal handler touches.
// ---------------------------------------------------------------------------
static std::atomic<bool> g_sigint_flag{false};

// FIX 1.2: Signal handler is now async-signal-safe.
// Only touches a single atomic<bool> — no mutex, no iostream, no file I/O.
void handle_sigint(int) {
    g_sigint_flag.store(true, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// .env file loader — reads KEY=VALUE pairs from a file and sets them as
// environment variables (only if not already set — env takes precedence).
// Skips blank lines and # comments. Strips surrounding quotes from values.
// ---------------------------------------------------------------------------
static void load_dotenv(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) return;  // no .env = silent skip

    std::string line;
    while (std::getline(f, line)) {
        // Strip trailing \r (Windows line endings)
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Skip blank lines and comments
        if (line.empty() || line[0] == '#') continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key   = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        // Strip "export " prefix — shell-style .env files use "export KEY=val"
        if (key.substr(0, 7) == "export ") {
            key = key.substr(7);
        }
        // Strip any remaining whitespace around key
        while (!key.empty() && key.back() == ' ') key.pop_back();

        // Strip leading whitespace from value
        size_t vs = 0;
        while (vs < value.size() && value[vs] == ' ') vs++;
        if (vs > 0) value = value.substr(vs);

        // Strip surrounding quotes (single or double)
        if (value.size() >= 2) {
            char q = value.front();
            if ((q == '"' || q == '\'') && value.back() == q) {
                value = value.substr(1, value.size() - 2);
            }
        }

        // Only set if not already in environment (env vars take precedence)
        if (!std::getenv(key.c_str())) {
            setenv(key.c_str(), value.c_str(), 0);
        }
    }

    std::cout << "[CHIMERA] .env loaded from " << path << "\n";
}

int main() {
    // ---------------------------------------------------------------------------
    // Load .env file before anything else — credentials must be available
    // before REST clients or WS user stream are constructed.
    // Search order: ./.env, ../.env (for running from build/ subdir).
    // ---------------------------------------------------------------------------
    load_dotenv(".env");
    load_dotenv("../.env");

    {
        const char* key    = std::getenv("BINANCE_API_KEY");
        const char* secret = std::getenv("BINANCE_API_SECRET");
        if (key && secret) {
            std::cout << "[CHIMERA] API keys loaded from environment\n";
        } else {
            std::cout << "[CHIMERA] WARNING: No BINANCE_API_KEY/SECRET in .env — shadow only\n";
        }
    }
    // ---------------------------------------------------------------------------
    // CURL: process-wide init. Must happen exactly once, before any REST client
    // is constructed. Removed from BinanceRestClient, OKXRestClient, BybitRestClient
    // constructors to eliminate redundant repeated calls.
    // ---------------------------------------------------------------------------
    curl_global_init(CURL_GLOBAL_ALL);

    // ---- CONTEXT: single owner of all state ----
    Context ctx;

    // ---- SNAPSHOT: load prior state if exists ----
    ContextSnapshotter snap(ctx);
    snap.load("/var/log/chimera/snapshot.bin");

    // ---- SIGNAL HANDLERS (after snapshot load) ----
    std::signal(SIGINT,  handle_sigint);
    std::signal(SIGTERM, handle_sigint);

    // ---- TELEMETRY SEED ----
    // Populate symbols map immediately so /state JSON shows them from first boot.
    // Actual position/notional values are updated live by ExecutionRouter on
    // each shadow fill. Without this seed, symbols:{} stays empty until the
    // first fill completes — which never happens if risk/throttle block everything.
    ctx.telemetry.update_symbol("BTCUSDT", 0.0, 0.0);
    ctx.telemetry.update_symbol("ETHUSDT", 0.0, 0.0);
    ctx.telemetry.update_symbol("SOLUSDT", 0.0, 0.0);

    std::cout << "[CHIMERA] Telemetry seeded: BTCUSDT, ETHUSDT, SOLUSDT\n";
    std::cout << "[CHIMERA] CPU cores: " << CpuPinning::cores() << "\n";
    std::cout << "[CHIMERA] ARM: " << ctx.arm.status() << "\n";

    // ---- PNL GOVERNOR DEFAULTS ----
    ctx.pnl.set_strategy_floor(-10.0);   // kill strategy if rolling EV < -$10
    ctx.pnl.set_portfolio_dd(-500.0);    // kill portfolio if total PnL < -$500
    std::cout << "[CHIMERA] PnL governor: strategy_floor=-10.0, portfolio_dd=-500.0\n";

    // ---- QUEUE DECAY GOVERNOR ----
    QueueDecayGovernor queue_decay(ctx);
    ctx.queue_decay = &queue_decay;
    std::cout << "[CHIMERA] Queue decay: hard_ttl=5s, soft_ttl=1s\n";

    // ---- EDGE ATTRIBUTION ----
    EdgeAttribution edge(ctx);
    ctx.edge = &edge;
    edge.set_max_edge_leak_bps(1.5);
    edge.set_max_latency_sensitivity(0.002);
    std::cout << "[CHIMERA] Edge attribution: max_leak=1.5bps, max_lat_sens=0.002\n";

    // ---- DESK ARBITER ----
    DeskArbiter desk(ctx);
    ctx.desk = &desk;
    // Register engines → desks. Engine IDs must match what engines return from id().
    desk.register_engine("BTC_CASCADE", "BTC_DESK");
    desk.register_engine("ETH_SNIPER",  "ETH_DESK");
    desk.register_engine("MEAN_REV",    "MEAN_REV_DESK");
    std::cout << "[CHIMERA] Desk arbiter: 3 desks registered (BTC, ETH, MEAN_REV)\n";

    // ---- COMPONENTS ----
    ExecutionRouter   router(ctx);
    HttpServer        http(8080, ctx);

    // ---- STRATEGY LAYER ----
    // StrategyContext bridges engines to ExecutionRouter + QueuePositionModel.
    // Engines are unchanged — StrategyRunner wraps each in a polling thread
    // that reads the book, calls onTick, and submits any OrderIntents.
    StrategyContext strat_ctx(ctx, router);

    BTCascade            eng_btc;
    ETHSniper            eng_eth;
    MeanReversion        eng_mean;
    QueueMarketMaker     eng_qmm;
    ImpulseReversion     eng_imp;
    PortfolioSkewTrader  eng_pst;
    ETHFade              eng_ethfade;
    SOLFade              eng_solfade;

    StrategyRunner  run_btc(&eng_btc,  strat_ctx);
    StrategyRunner  run_eth(&eng_eth,  strat_ctx);
    StrategyRunner  run_mean(&eng_mean, strat_ctx);
    StrategyRunner  run_qmm(&eng_qmm, strat_ctx);
    StrategyRunner  run_imp(&eng_imp, strat_ctx);
    StrategyRunner  run_pst(&eng_pst, strat_ctx);
    StrategyRunner  run_ethfade(&eng_ethfade, strat_ctx);
    StrategyRunner  run_solfade(&eng_solfade, strat_ctx);

    // ---------------------------------------------------------------------------
    // Trade mode selection: BINANCE_TRADE_MODE env var.
    //   "futures" → fapi.binance.com / fstream.binance.com (USDT-M perps)
    //   "spot"    → api.binance.com  / stream.binance.com  (default)
    //
    // Local Mac: spot (NZ blocks futures REST from local IPs).
    // VPS deploy: set BINANCE_TRADE_MODE=futures — VPS IP whitelisted on futures key.
    // WSMarket/WSUser read this env var internally for stream host selection.
    // ---------------------------------------------------------------------------
    const char* trade_mode_env = std::getenv("BINANCE_TRADE_MODE");
    bool futures_mode = (trade_mode_env && std::string(trade_mode_env) == "futures");
    const char* rest_base = futures_mode ? "https://fapi.binance.com" : "https://api.binance.com";
    std::cout << "[CHIMERA] Trade mode: " << (futures_mode ? "FUTURES (USDT-M)" : "SPOT") << "\n";

    // Binance direct feeds — Context& injected for book + fill wiring.
    // ---------------------------------------------------------------------------
    // OKX + Bybit disabled until go-live. Binance only for shadow validation.
    // ---------------------------------------------------------------------------
    BinanceWSMarket binance_market(ctx, futures_mode ? "wss://fstream.binance.com" : "wss://stream.binance.com");
    BinanceWSUser   binance_user(ctx, rest_base);

    std::cout << "[CHIMERA] Binance: MAINNET\n";

    // ---------------------------------------------------------------------------
    // OKX + Bybit disabled until go-live. Binance only for shadow validation.
    // Re-enable by adding MultiVenueManager + OKXAdapter + BybitAdapter.
    // ---------------------------------------------------------------------------

    // =========================================================================
    // COLD START GATES — DISABLED FOR SHADOW TESTING
    // Uncomment both blocks before go-live. They are mandatory for live capital:
    //   Block 1: OKX + Bybit position/order reconciliation
    //   Block 2: Binance position/order reconciliation
    // =========================================================================

    // ---- COLD START GATE (OKX + BYBIT) ----
    // ColdStartReconciler reconciler(ctx);
    // bool recon_ok = reconciler.reconcile(venues.adapters());
    // std::cout << reconciler.report();
    //
    // if (!recon_ok) {
    //     std::cerr << "[FATAL] Cold start failed. System locked.\n";
    //     snap.save("/var/log/chimera/snapshot.bin");
    //     return 1;
    // }
    //
    // std::cout << "[CHIMERA] Cold start PASSED. Arm sequence available.\n";

    // ---- BINANCE COLD-START RECONCILIATION ----
    // {
    //     const char* key    = std::getenv("BINANCE_API_KEY");
    //     const char* secret = std::getenv("BINANCE_API_SECRET");
    //     if (key && secret) {
    //         BinanceAuth       bauth(key, secret);
    //         BinanceRestClient brest(rest_base, bauth);
    //         BinanceReconciler brecon(brest);
    //
    //         bool binance_clean = brecon.reconcile();
    //         std::cout << brecon.report();
    //
    //         if (!binance_clean) {
    //             std::cerr << "[FATAL] Binance reconciliation failed. System locked.\n";
    //             snap.save("/var/log/chimera/snapshot.bin");
    //             return 1;
    //         }
    //         std::cout << "[CHIMERA] Binance reconciliation PASSED.\n";
    //     } else {
    //         std::cout << "[CHIMERA] Binance keys not set — reconciliation skipped (shadow mode).\n";
    //     }
    // }

    std::cout << "[CHIMERA] Cold start gates DISABLED (shadow testing)\n";

    // ---- LIVE EXECUTION: WS Trading API (hot path) + REST (sweep fallback) ----
    // BinanceWSExecution owns the persistent WS connection to Binance WS Trading API.
    // All order submit + cancel on the hot path goes through this — preserves 0.2ms
    // latency advantage. REST is retained ONLY for cancel federation sweep
    // (fire-and-forget when system is dying) and ExchangeTruthLoop.
    BinanceWSExecution ws_exec(ctx);

    std::unique_ptr<BinanceAuth>      live_auth;
    std::unique_ptr<BinanceRestClient> live_rest;
    {
        const char* key    = std::getenv("BINANCE_API_KEY");
        const char* secret = std::getenv("BINANCE_API_SECRET");
        if (key && secret) {
            live_auth = std::make_unique<BinanceAuth>(key, secret);
            live_rest = std::make_unique<BinanceRestClient>(
                rest_base, *live_auth);

            // Hot path: WS exec
            router.set_ws_exec(&ws_exec);
            ws_exec.start();

            // Cold path: REST for federation sweep + reconcile
            router.set_rest_client(live_rest.get());

            std::cout << "[CHIMERA] Live execution: WS Trading API (hot) + REST (sweep fallback)\n";
        } else {
            std::cout << "[CHIMERA] No Binance keys — live execution disabled (shadow mode)\n";
        }
    }

    std::cout << "[CHIMERA] Trading: gated by LiveArmSystem (arm + verify + WS alive)\n";

    // ---- EXCHANGE TRUTH LOOP ----
    // Periodic live verification of exchange state vs local state.
    // Runs on its own thread — needs a DEDICATED BinanceRestClient because
    // CURL easy handles are not thread-safe. Cannot share live_rest_.
    // nullptr if no credentials (shadow mode — loop is a no-op anyway).
    std::unique_ptr<BinanceAuth>       truth_auth;
    std::unique_ptr<BinanceRestClient> truth_rest;
    ExchangeTruthLoop truth_loop(ctx, std::chrono::seconds(3));
    {
        const char* key    = std::getenv("BINANCE_API_KEY");
        const char* secret = std::getenv("BINANCE_API_SECRET");
        if (key && secret) {
            truth_auth = std::make_unique<BinanceAuth>(key, secret);
            truth_rest = std::make_unique<BinanceRestClient>(
                rest_base, *truth_auth);
            truth_loop.set_rest_client(truth_rest.get());
            std::cout << "[CHIMERA] Exchange truth loop: 3s interval, REST client wired\n";
        } else {
            std::cout << "[CHIMERA] Exchange truth loop: no keys — disabled (shadow mode)\n";
        }
    }

    // ---- THREADS ----
    // FIX 1.1: binance_market.run() blocks in reconnect loop. If called sequentially
    // before binance_user.run(), the user stream NEVER starts — it's sequenced after
    // the blocking market loop which runs forever.
    //
    // Fix: each gets its own ThreadModel. Both are pinned to CORE0 (feed threads).
    // The OS time-shares them on CORE0. Neither blocks the other.
    //
    // FIX 4.2: venues.start(0) — all venue threads pinned to CORE0.
    // Previously: venues.start() spawned threads with no affinity.

    // CORE0: market feeds — each in own thread
    ThreadModel core0_market(0, [&]() {
        binance_market.run(ctx.running);
    });

    ThreadModel core0_user(0, [&]() {
        binance_user.run(ctx.running);
    });

    // CORE1: execution tick
    ThreadModel core1(1, [&]() {
        while (ctx.running.load()) {
            router.poll();
            desk.poll();
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });

    // Telemetry: HTTP server (non-blocking accept, exits on ctx.running=false)
    ThreadModel telemetry_thread(0, [&]() {
        http.run();
    });

    // CORE1: strategy engines — each in own thread, all pinned to CORE1.
    // Engines poll the book independently. ExecutionRouter::poll() (also CORE1)
    // drains the shadow fill queue. OS time-shares all CORE1 threads.
    ThreadModel strat_btc_thread(1, [&]() {
        run_btc.run(ctx.running);
    });

    ThreadModel strat_eth_thread(1, [&]() {
        run_eth.run(ctx.running);
    });

    ThreadModel strat_mean_thread(1, [&]() {
        run_mean.run(ctx.running);
    });

    ThreadModel strat_qmm_thread(1, [&]() {
        run_qmm.run(ctx.running);
    });

    ThreadModel strat_imp_thread(1, [&]() {
        run_imp.run(ctx.running);
    });

    ThreadModel strat_pst_thread(1, [&]() {
        run_pst.run(ctx.running);
    });

    ThreadModel strat_ethfade_thread(1, [&]() {
        run_ethfade.run(ctx.running);
    });

    ThreadModel strat_solfade_thread(1, [&]() {
        run_solfade.run(ctx.running);
    });

    core0_market.start();
    core0_user.start();
    core1.start();
    telemetry_thread.start();
    strat_btc_thread.start();
    strat_eth_thread.start();
    strat_mean_thread.start();
    strat_qmm_thread.start();
    strat_imp_thread.start();
    strat_pst_thread.start();
    strat_ethfade_thread.start();
    strat_solfade_thread.start();
    truth_loop.start();

    // Main thread: telemetry pump + signal check + console display
    auto start_time   = std::chrono::steady_clock::now();
    auto last_print   = std::chrono::steady_clock::now();
    constexpr int     PRINT_INTERVAL_S = 5;

    while (ctx.running.load()) {
        // FIX 1.2: Check signal flag in main thread — not in signal handler.
        // Signal handler only sets the flag. Main thread acts on it here,
        // safely outside signal context.
        if (g_sigint_flag.load()) {
            ctx.running.store(false);
            break;
        }

        auto now    = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
        ctx.telemetry.set_uptime(uptime);
        ctx.telemetry.set_drift(ctx.risk.killed());

        // ---------------------------------------------------------------------------
        // Console display — 5s interval. Mirrors what the HTTP endpoint exposes
        // but formatted for terminal readability. Shows: uptime, arm state, PnL,
        // fills, per-symbol positions, book state, and per-strategy stats.
        // ---------------------------------------------------------------------------
        auto since_print = std::chrono::duration_cast<std::chrono::seconds>(now - last_print).count();
        if (since_print >= PRINT_INTERVAL_S) {
            last_print = now;

            // Gather state
            double portfolio_pnl    = ctx.pnl.portfolio_pnl();
            uint64_t total_fills    = ctx.telemetry.total_fills();
            uint64_t throttle_blks  = ctx.telemetry.throttle_blocks();
            uint64_t risk_blks      = ctx.telemetry.risk_blocks();
            bool     arm_live       = ctx.arm.live_enabled();
            bool     killed         = ctx.risk.killed();
            auto     positions      = ctx.risk.dump_positions();
            auto     strat_stats    = ctx.pnl.dump_stats();
            auto     books          = ctx.queue.dump_books();

            // Print
            std::cout << "\n"
                      << "═══════════════════════════════════════════════════════════\n"
                      << " CHIMERA SHADOW  |  uptime=" << uptime << "s"
                      << "  arm=" << (arm_live ? "LIVE" : "DISARMED")
                      << "  " << (killed ? "⚠ KILLED" : "OK") << "\n"
                      << "───────────────────────────────────────────────────────────\n"
                      << " PnL: $" << std::fixed << std::setprecision(4) << portfolio_pnl
                      << "    fills=" << total_fills
                      << "  throttle_blocks=" << throttle_blks
                      << "  risk_blocks=" << risk_blks << "\n"
                      << "───────────────────────────────────────────────────────────\n"
                      << " POSITIONS + BOOK\n";

            static const char* SYMS[] = {"BTCUSDT", "ETHUSDT", "SOLUSDT"};
            for (const char* sym : SYMS) {
                double pos = 0.0;
                auto pit = positions.find(sym);
                if (pit != positions.end()) pos = pit->second;

                double bid = 0.0, ask = 0.0;
                auto bit = books.find(sym);
                if (bit != books.end()) {
                    bid = bit->second.bid_price;
                    ask = bit->second.ask_price;
                }

                std::cout << "   " << sym
                          << "  pos=" << std::setprecision(6) << pos
                          << "  bid=" << std::setprecision(2) << bid
                          << "  ask=" << ask << "\n";
            }

            std::cout << "───────────────────────────────────────────────────────────\n"
                      << " STRATEGIES\n";
            for (const auto& kv : strat_stats) {
                std::cout << "   " << kv.first
                          << "  pnl=$" << std::setprecision(4) << kv.second.realized_pnl
                          << "  ev=" << kv.second.rolling_ev
                          << (kv.second.killed ? "  [KILLED]" : "") << "\n";
            }
            std::cout << "═══════════════════════════════════════════════════════════\n";
            std::cout << std::flush;
        }

        // Main loop sleeps briefly to avoid busy-wait.
        // 100ms = fast shutdown response while keeping CPU usage low.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[CHIMERA] Shutdown initiated\n";

    ws_exec.stop();
    truth_loop.stop();
    core0_market.stop();
    core0_user.stop();
    core1.stop();
    telemetry_thread.stop();
    strat_btc_thread.stop();
    strat_eth_thread.stop();
    strat_mean_thread.stop();
    strat_qmm_thread.stop();
    strat_imp_thread.stop();
    strat_pst_thread.stop();
    strat_ethfade_thread.stop();
    strat_solfade_thread.stop();

    core0_market.join();
    core0_user.join();
    core1.join();
    telemetry_thread.join();
    strat_btc_thread.join();
    strat_eth_thread.join();
    strat_mean_thread.join();
    strat_qmm_thread.join();
    strat_imp_thread.join();
    strat_pst_thread.join();
    strat_ethfade_thread.join();
    strat_solfade_thread.join();

    std::cout << "[CHIMERA] All threads stopped, saving snapshot...\n";

    // FIX 1.2: Snapshot save happens here in main thread — NOT in signal handler.
    // This is safe: no signal context, full iostream/mutex/file access available.
    snap.save("/var/log/chimera/snapshot.bin");

    std::cout << "[CHIMERA] Clean exit\n";
    return 0;
}
