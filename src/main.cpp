#include "gui/WsServer.hpp"
#include "shadow/MultiSymbolExecutor.hpp"
#include "execution/ExecutionRouter.hpp"
#include "gui/TradeHistory.hpp"

WsServer* g_wsServer = nullptr;
shadow::MultiSymbolExecutor* g_executor = nullptr;

#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstring>

#include "shadow/CrashHandler.hpp"
#include "shadow/WatchdogThread.hpp"
#include "shadow/JournalWriter.hpp"
#include "shadow/EquityCurve.hpp"
#include "gui/GUIBroadcaster.hpp"
#include "fix/CTraderFIXClient.hpp"
#include "fix/FIXConfig.hpp"

static std::atomic<bool> g_running{true};

// ============================================================================
// METALS SIGNAL GENERATOR - XAU TUNED FOR QUALITY
// ============================================================================
class MetalsSignalGenerator {
public:
    explicit MetalsSignalGenerator(shadow::MultiSymbolExecutor& executor)
        : executor_(executor) {}

    // Called by exit callback to track last exit for displacement filter
    void onExit(const std::string& symbol, double exit_price, double pnl) {
        auto& state = states_[symbol];
        state.last_exit_price = exit_price;
        state.last_exit_pnl = pnl;
        state.last_exit_ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    void onTick(const std::string& symbol, double bid, double ask, uint64_t ts_ns) {
        if (symbol != "XAUUSD" && symbol != "XAGUSD") return;
        
        auto& state = states_[symbol];
        
        double mid = (bid + ask) / 2.0;
        double spread = ask - bid;
        
        if (spread <= 0.0) return;
        
        // Initialize EMA
        if (state.tick_count == 0) {
            state.ema_fast = mid;
            state.ema_slow = mid;
            state.last_mid = mid;
            state.tick_count = 1;
            return;
        }
        
        // Update EMAs
        state.ema_fast = 0.3 * mid + 0.7 * state.ema_fast;
        state.ema_slow = 0.1 * mid + 0.9 * state.ema_slow;
        
        // Calculate momentum
        double momentum = mid - state.last_mid;
        state.momentum = 0.3 * momentum + 0.7 * state.momentum;
        
        state.last_mid = mid;
        state.tick_count++;
        
        // Need warmup
        if (state.tick_count < 20) return;
        
        // Trend detection
        bool uptrend = state.ema_fast > state.ema_slow;
        bool downtrend = state.ema_fast < state.ema_slow;
        
        double threshold = spread * 0.25;  // 25% of spread
        
        // ═══════════════════════════════════════════════════════════
        // XAU FIX 1: MINIMUM DISPLACEMENT FROM LAST EXIT ($0.60)
        // Prevents chop re-entry immediately after exit
        // ═══════════════════════════════════════════════════════════
        if (symbol == "XAUUSD" && state.last_exit_price > 0) {
            double displacement = std::abs(mid - state.last_exit_price);
            if (displacement < 0.60) {
                return;  // Too close to last exit - avoid whipsaw
            }
        }
        
        // ═══════════════════════════════════════════════════════════
        // XAU FIX 2: EXTENDED COOLDOWN AFTER LOSING EXIT (3s vs 1s)
        // Gold whipsaws after losses - stay out longer
        // ═══════════════════════════════════════════════════════════
        uint64_t cooldown_ns = 1'000'000'000ULL;  // 1 second base (Silver)
        
        if (symbol == "XAUUSD") {
            if (state.last_exit_pnl < 0) {
                cooldown_ns = 3'000'000'000ULL;  // 3 seconds after XAU loss
            }
        }
        
        if (ts_ns - state.last_signal_ns < cooldown_ns) {
            return;  // In cooldown
        }
        
        // Price must have changed enough from last signal
        double min_price_change = (symbol == "XAUUSD") ? 0.10 : 0.05;
        if (state.last_signal_price > 0 && 
            std::abs(mid - state.last_signal_price) < min_price_change) {
            return;
        }
        
        // ═══════════════════════════════════════════════════════════
        // XAU FIX 3: 2-TICK CONFIRMATION RULE
        // Require two consecutive directional ticks before XAU signal
        // Filters single-tick noise without adding lag
        // ═══════════════════════════════════════════════════════════
        if (symbol == "XAUUSD") {
            // Determine current directional signal
            bool current_up = uptrend && state.momentum > 0 && std::abs(state.momentum) > threshold;
            bool current_down = downtrend && state.momentum < 0 && std::abs(state.momentum) > threshold;
            
            if (current_up) {
                if (state.last_direction == 1) {
                    // Two consecutive up ticks - CONFIRMED BUY
                    emitBuySignal(symbol, mid);
                    state.last_signal_ns = ts_ns;
                    state.last_signal_price = mid;
                    state.last_direction = 0;  // Reset after emit
                } else {
                    state.last_direction = 1;  // First up tick, wait for second
                }
            } else if (current_down) {
                if (state.last_direction == -1) {
                    // Two consecutive down ticks - CONFIRMED SELL
                    emitSellSignal(symbol, mid);
                    state.last_signal_ns = ts_ns;
                    state.last_signal_price = mid;
                    state.last_direction = 0;  // Reset after emit
                } else {
                    state.last_direction = -1;  // First down tick, wait for second
                }
            } else {
                state.last_direction = 0;  // No signal this tick, reset
            }
        } else {
            // SILVER: Single-tick signals (no 2-tick confirmation needed)
            if (uptrend && state.momentum > 0 && std::abs(state.momentum) > threshold) {
                emitBuySignal(symbol, mid);
                state.last_signal_ns = ts_ns;
                state.last_signal_price = mid;
            } else if (downtrend && state.momentum < 0 && std::abs(state.momentum) > threshold) {
                emitSellSignal(symbol, mid);
                state.last_signal_ns = ts_ns;
                state.last_signal_price = mid;
            }
        }
    }

private:
    struct State {
        double ema_fast = 0.0;
        double ema_slow = 0.0;
        double momentum = 0.0;
        double last_mid = 0.0;
        uint64_t tick_count = 0;
        uint64_t last_signal_ns = 0;
        double last_signal_price = 0.0;
        double last_exit_price = 0.0;    // For FIX 1: displacement filter
        double last_exit_pnl = 0.0;       // For FIX 2: cooldown after loss
        uint64_t last_exit_ts_ns = 0;
        int last_direction = 0;           // For FIX 3: -1=down, 0=none, 1=up
    };
    
    std::unordered_map<std::string, State> states_;
    shadow::MultiSymbolExecutor& executor_;
    
    void emitBuySignal(const std::string& symbol, double price) {
        shadow::Signal sig;
        sig.side = shadow::Side::BUY;
        sig.price = price;
        sig.confidence = 0.75;
        
        std::cout << "[SIGNAL] " << symbol << " BUY @ " << price << "\n";
        executor_.onSignal(symbol, sig);
    }
    
    void emitSellSignal(const std::string& symbol, double price) {
        shadow::Signal sig;
        sig.side = shadow::Side::SELL;
        sig.price = price;
        sig.confidence = 0.75;
        
        std::cout << "[SIGNAL] " << symbol << " SELL @ " << price << "\n";
        executor_.onSignal(symbol, sig);
    }
};

static void signal_handler(int sig) {
    std::cout << "\n[SIGNAL] Caught " << sig << "\n";
    g_running.store(false);
}

int main(int argc, char* argv[]) {
    using namespace shadow;

    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "  CHIMERA v4.35 - METALS PRODUCTION\n";
    std::cout << "═══════════════════════════════════════════════════════════\n\n";

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    shadow::CrashHandler::install();
    shadow::JournalWriter::init();

    WsServer wsServer(7777);
    wsServer.start();
    g_wsServer = &wsServer;

    ExecMode mode = ExecMode::SHADOW;

    MultiSymbolExecutor executor;
    g_executor = &executor;

    SymbolConfig xau;
    xau.symbol = "XAUUSD";
    xau.max_legs = 3;
    xau.base_size = 1.0;
    xau.initial_stop = 0.45;
    executor.addSymbol(xau, mode);

    SymbolConfig xag;
    xag.symbol = "XAGUSD";
    xag.max_legs = 2;
    xag.base_size = 1.0;
    xag.initial_stop = 0.55;
    executor.addSymbol(xag, mode);

    std::cout << "[INIT] Configured: XAUUSD, XAGUSD\n";

    Chimera::GUIBroadcaster gui;
    gui.start();
    std::cout << "[INIT] GUI started\n";

    auto xau_exec = executor.getExecutor("XAUUSD");
    auto xag_exec = executor.getExecutor("XAGUSD");

    // SIGNAL GENERATOR - XAU tuned for quality
    MetalsSignalGenerator signalGen(executor);
    std::cout << "[INIT] Signal generator initialized\n";

    // Wire EXIT callbacks (called only on position close)
    if (xau_exec) {
        xau_exec->setExitCallback([&signalGen](const char* sym, uint64_t trade_id,
                                                 double exit_price, double pnl, const char* reason) {
            signalGen.onExit(sym, exit_price, pnl);
        });
    }

    if (xag_exec) {
        xag_exec->setExitCallback([&signalGen](const char* sym, uint64_t trade_id,
                                                 double exit_price, double pnl, const char* reason) {
            signalGen.onExit(sym, exit_price, pnl);
        });


    // Wire GUI callbacks for trade blotter
    if (xau_exec) {
        xau_exec->setGUICallback([&gui](const char* sym, uint64_t trade_id, char side,
                                         double entry, double exit_price, double size,
                                         double pnl, uint64_t ts_ms) {
            const char* side_str = (side == 'B') ? "BUY" : "SELL";
            gui.broadcastTrade(sym, side_str, size, entry, pnl);
            
            // Add to trade history for blotter
            gui::TradeRecord trade;
            trade.id = trade_id;
            trade.sym = sym;
            trade.side = side;
            trade.qty = size;
            trade.entry = entry;
            trade.exit = exit_price;
            trade.fees = 0.0; // TODO: calculate actual fees
            trade.pnl = pnl;
            gui::TradeHistory::instance().addTrade(trade);
        });
    }
    if (xag_exec) {
        xag_exec->setGUICallback([&gui](const char* sym, uint64_t trade_id, char side,
                                         double entry, double exit_price, double size,
                                         double pnl, uint64_t ts_ms) {
            const char* side_str = (side == 'B') ? "BUY" : "SELL";
            gui.broadcastTrade(sym, side_str, size, entry, pnl);
            
            // Add to trade history for blotter
            gui::TradeRecord trade;
            trade.id = trade_id;
            trade.sym = sym;
            trade.side = side;
            trade.qty = size;
            trade.entry = entry;
            trade.exit = exit_price;
            trade.fees = 0.0; // TODO: calculate actual fees
            trade.pnl = pnl;
            gui::TradeHistory::instance().addTrade(trade);
        });
    }


    }

    std::cout << "[INIT] Loading FIX config...\n";
    Chimera::FIXConfig fixConfig;

    if (!fixConfig.isValid()) {
        std::cerr << "[FATAL] Invalid FIX configuration\n";
        return 1;
    }

    fixConfig.print();

    Chimera::CTraderFIXClient fix;
    fix.setConfig(fixConfig);

    // Tick callback: ticks -> signal generator -> executor
    fix.setOnTick([&executor, &signalGen](const Chimera::CTraderTick& tick) {
        uint64_t ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();

        shadow::Tick t;
        t.bid = tick.bid;
        t.ask = tick.ask;
        t.ts_ms = ts_ns / 1'000'000;

        // Feed tick to executor for position tracking
        executor.onTick(tick.symbol, t);
        
        // Feed tick to signal generator
        signalGen.onTick(tick.symbol, tick.bid, tick.ask, ts_ns);
    });

    
    // RTT Recording: Feed FIX latency to LatencyMonitor
    
    // RTT Recording - feeds LatencyRouter
    fix.setOnLatency([&executor](const std::string& symbol, double rtt_ms, double slippage_bps) {
        uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        executor.router().on_fix_rtt(rtt_ms, now_ms);
        (void)symbol; (void)slippage_bps;
    });
    
    fix.setOnExec([](const Chimera::CTraderExecReport& exec) {
        std::cout << "[EXEC] Order " << exec.clOrdID << " executed\n";
    });

    std::cout << "[INIT] Connecting to cTrader FIX...\n";

    if (!fix.connect()) {
        std::cerr << "[FATAL] FIX connection failed\n";
        gui.stop();
        return 1;
    }

    std::cout << "[INIT] Connected\n";
    
    std::cout << "[INIT] Requesting security list...\n";
    fix.requestSecurityList();
    
    std::cout << "[INIT] Waiting for security list...\n";
    for (int i = 0; i < 100 && !fix.isSecurityListReady(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    if (!fix.isSecurityListReady()) {
        std::cerr << "[FATAL] Security list timeout\n";
        gui.stop();
        return 1;
    }
    
    std::cout << "[INIT] Security list loaded (" << fix.getSecurityListCount() << " symbols)\n";
    
    std::cout << "[INIT] Subscribing to market data...\n";
    fix.subscribeMarketData("XAUUSD");
    fix.subscribeMarketData("XAGUSD");
    std::cout << "[INIT] Market data subscriptions sent\n";

    std::cout << "[MAIN] System running with MetalsSignalGenerator\n";
    std::cout << "[MAIN] Press Ctrl+C to stop.\n\n";

    shadow::WatchdogThread::start();

    uint64_t ticks = 0;
    auto last_status = std::chrono::steady_clock::now();

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ticks++;

        shadow::WatchdogThread::heartbeat();

        
        // Poll FIX RTT and feed to LatencyRouter
        double rtt = fix.fixRttLastMs();
        if (rtt > 0) {
            uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            executor.router().on_fix_rtt(rtt, now_ms);
            executor.router().on_loop_heartbeat(now_ms);
        }
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_status).count() >= 10) {
            executor.statusAll();
            std::cout << "[STATUS] Ticks: " << ticks
                      << " PnL: $" << executor.getTotalRealizedPnl() << "\n";
            
            auto* xau = executor.getExecutor("XAUUSD");
            auto* xag = executor.getExecutor("XAGUSD");
            if (xau) {
                std::cout << "[XAU] bid=" << xau->getLastBid() 
                          << " ask=" << xau->getLastAsk()
                          << " legs=" << xau->getActiveLegs()
                          << " pnl=$" << xau->getRealizedPnL() << "\n";
            }
            if (xag) {
                std::cout << "[XAG] bid=" << xag->getLastBid()
                          << " ask=" << xag->getLastAsk() 
                          << " legs=" << xag->getActiveLegs()
                          << " pnl=$" << xag->getRealizedPnL() << "\n";
            }
            
            last_status = now;
        }
    }

    std::cout << "\n[SHUTDOWN] Stopping...\n";

    shadow::WatchdogThread::stop();
    fix.disconnect();
    gui.stop();
    wsServer.stop();

    std::cout << "\n[SHUTDOWN] Final status:\n";
    executor.statusAll();
    std::cout << "\n[SHUTDOWN] Complete. PnL: $"
              << executor.getTotalRealizedPnl() << "\n";

    return 0;
}
