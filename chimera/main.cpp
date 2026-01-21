#include <iostream>
#include <csignal>
#include <atomic>
#include <chrono>
#include <iomanip>

#include "core/system_state.hpp"
#include "core/engine_signal.hpp"
#include "core/tick_snapshot.hpp"
#include "core/event_bus.hpp"
#include "core/signal_bridge.hpp"
#include "core/lag_model.hpp"

#include "core/ofi_engine.hpp"
#include "core/depth_engine.hpp"
#include "core/liquidation_engine.hpp"
#include "core/impulse_engine.hpp"
#include "core/btc_cascade.hpp"

#include "core/streams.hpp"
#include "core/capital_allocator.hpp"

#include "market/binance_adapter.hpp"
#include "exec/binance_executor.hpp"
#include "logging/bchs_logger.hpp"

OFIEngine g_ofi;
DepthEngine g_depth;
LiquidationEngine g_liq;
ImpulseEngine g_impulse;

SignalBridge g_bridge;
LagModel g_lag;
EventBus<CascadeEvent> g_bus;

BTCCascade g_cascade(g_ofi, g_depth, g_liq, g_impulse, g_bridge, g_bus);

FollowerStream g_eth("ETHUSDT", g_lag, g_bridge);
FollowerStream g_sol("SOLUSDT", g_lag, g_bridge);

CapitalAllocator g_alloc;
BinanceExecutor g_exec;
BCHSLogger g_logger("chimera_events.csv");

std::atomic<double> g_btc_price{0.0};
std::atomic<double> g_btc_spread{0.0};
std::atomic<uint64_t> g_last_ts{0};

std::atomic<bool> g_running{true};

void signalHandler(int) {
    g_running.store(false);
}

void onCascade(const CascadeEvent& ev) {
    g_eth.onCascade(ev);
    g_sol.onCascade(ev);
}

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    std::cout << "=== CHIMERA v1.0 ===\n";
    std::cout << "Independent Engine Architecture\n";
    std::cout << "Engines: OFI | DEPTH | LIQ | IMPULSE\n\n";
    
    g_ofi.setThresholds(1.5, 0.2);
    g_depth.setCollapseThreshold(0.65);
    g_depth.setMinVacuumDuration(300'000'000ULL);
    g_liq.setThreshold(3'000'000.0);
    g_liq.setWindow(5'000'000'000ULL);
    g_impulse.setMinDisplacement(5.0);
    g_impulse.setMinVelocity(10.0);
    
    g_cascade.setMinConfirmations(3);
    g_cascade.setMaxSpread(5.0);
    g_cascade.setMaxHold(30'000'000'000ULL);
    g_cascade.setCooldown(5'000'000'000ULL);
    
    g_alloc.registerStream("BTC_CASCADE", 1.0);
    g_alloc.registerStream("ETH_FOLLOW", 0.7);
    g_alloc.registerStream("SOL_FOLLOW", 0.5);
    g_alloc.setMaxDrawdown(0.15);
    g_alloc.setKillThreshold(0.25);
    
    g_exec.risk().setMaxNotional(5000.0);
    g_exec.risk().setMaxPosition(0.1);
    g_exec.risk().setMaxDailyLoss(200.0);
    
    g_bus.subscribe(onCascade);
    
    g_exec.onFill([](const Fill& f) {
        g_logger.logFill(f, g_alloc.totalEquity());
    });
    
    g_exec.start();
    
    BinanceAdapter market;
    market.subscribe("BTCUSDT");
    market.subscribe("ETHUSDT");
    market.subscribe("SOLUSDT");
    
    market.onTick([](const Tick& t) {
        uint64_t now = t.ts_ns;
        g_last_ts.store(now, std::memory_order_relaxed);
        
        if (t.symbol == "BTCUSDT") {
            g_btc_price.store(t.price, std::memory_order_relaxed);
            g_btc_spread.store(t.spread_bps, std::memory_order_relaxed);
            
            g_impulse.ingest(t.price, now);
            g_lag.recordBTC(now, t.price);
            
            double spread = g_btc_spread.load(std::memory_order_relaxed);
            CascadeSignal sig = g_cascade.evaluate(now, spread);
            
            if (sig.fired && g_cascade.shouldTrade()) {
                if (g_alloc.allowed("BTC_CASCADE") && !g_alloc.killSwitch()) {
                    double size = g_alloc.sizeFor("BTC_CASCADE", 0.001);
                    
                    g_exec.placeMarket(
                        "BTCUSDT",
                        sig.side,
                        size,
                        false,
                        t.price,
                        t.spread_bps
                    );
                    
                    std::cout << "[CASCADE] FIRED: " << sideStr(sig.side)
                              << " | Confirmations: " << sig.confirmation_count
                              << " | OFI:" << (sig.ofi_confirmed ? "Y" : "N")
                              << " DEPTH:" << (sig.depth_confirmed ? "Y" : "N")
                              << " LIQ:" << (sig.liq_confirmed ? "Y" : "N")
                              << " IMP:" << (sig.impulse_confirmed ? "Y" : "N")
                              << "\n";
                }
                
                g_cascade.markExecuted();
            }
        }
        else if (t.symbol == "ETHUSDT") {
            g_lag.recordFollower("ETHUSDT", now, t.price);
            g_eth.onTick(now, t.price);
            
            if (g_eth.shouldTrade(now)) {
                if (g_alloc.allowed("ETH_FOLLOW") && !g_alloc.killSwitch()) {
                    double size = g_alloc.sizeFor("ETH_FOLLOW", 0.01);
                    
                    g_exec.placeMarket(
                        "ETHUSDT",
                        g_eth.side(),
                        size,
                        false,
                        t.price,
                        t.spread_bps
                    );
                }
                g_eth.markExecuted();
            }
        }
        else if (t.symbol == "SOLUSDT") {
            g_lag.recordFollower("SOLUSDT", now, t.price);
            g_sol.onTick(now, t.price);
            
            if (g_sol.shouldTrade(now)) {
                if (g_alloc.allowed("SOL_FOLLOW") && !g_alloc.killSwitch()) {
                    double size = g_alloc.sizeFor("SOL_FOLLOW", 0.1);
                    
                    g_exec.placeMarket(
                        "SOLUSDT",
                        g_sol.side(),
                        size,
                        false,
                        t.price,
                        t.spread_bps
                    );
                }
                g_sol.markExecuted();
            }
        }
    });
    
    market.onTrade([](const TradeTick& t) {
        if (t.symbol == "BTCUSDT") {
            g_ofi.ingest(t.qty, t.is_buy, t.ts_ns);
        }
    });
    
    market.onDepth([](const DepthUpdate& du) {
        if (du.symbol == "BTCUSDT" || du.symbol == "btcusdt") {
            double bid_sum = 0.0;
            double ask_sum = 0.0;
            
            for (size_t i = 0; i < du.bids.size() && i < 5; ++i)
                bid_sum += du.bids[i].qty;
            for (size_t i = 0; i < du.asks.size() && i < 5; ++i)
                ask_sum += du.asks[i].qty;
            
            g_depth.ingest(bid_sum, ask_sum, du.ts_ns);
        }
    });
    
    market.onLiquidation([](const LiquidationTick& l) {
        if (l.symbol == "BTCUSDT") {
            g_liq.ingest(l.notional, l.is_long, l.ts_ns);
            
            if (g_liq.totalIntensity() > 1'000'000.0) {
                std::cout << "[LIQ] " << (l.is_long ? "LONG" : "SHORT") 
                          << " $" << std::fixed << std::setprecision(0) << l.notional
                          << " (total: $" << g_liq.totalIntensity() << ")\n";
            }
        }
    });
    
    std::cout << "Connecting to Binance Futures...\n";
    market.connect();
    
    auto last_status = std::chrono::steady_clock::now();
    
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        uint64_t now = g_last_ts.load(std::memory_order_relaxed);
        g_liq.decay(now);
        
        auto clock_now = std::chrono::steady_clock::now();
        if (clock_now - last_status > std::chrono::seconds(30)) {
            last_status = clock_now;
            
            OFISignal ofi_sig = g_ofi.evaluate(now);
            DepthSignal depth_sig = g_depth.evaluate(now);
            LiqSignal liq_sig = g_liq.evaluate(now);
            ImpulseSignal imp_sig = g_impulse.evaluate(now);
            
            std::cout << "\n[STATUS] BTC: $" << std::fixed << std::setprecision(2) 
                      << g_btc_price.load()
                      << " | Spread: " << std::setprecision(1) << g_btc_spread.load() << "bps"
                      << " | State: " << stateStr(g_cascade.state())
                      << "\n         OFI z=" << std::setprecision(2) << ofi_sig.zscore 
                      << " a=" << ofi_sig.accel << (ofi_sig.fired ? " [FIRE]" : "")
                      << "\n         DEPTH r=" << std::setprecision(3) << depth_sig.depth_ratio
                      << " vac=" << depth_sig.vacuum_duration_ns/1e6 << "ms" 
                      << (depth_sig.fired ? " [FIRE]" : "")
                      << "\n         LIQ $" << std::setprecision(0) << liq_sig.intensity
                      << (liq_sig.fired ? " [FIRE]" : "")
                      << "\n         IMP d=" << std::setprecision(1) << imp_sig.displacement_bps 
                      << "bps v=" << imp_sig.velocity << (imp_sig.fired ? " [FIRE]" : "")
                      << "\n";
        }
        
        if (g_alloc.killSwitch()) {
            std::cout << "[KILL SWITCH] Maximum drawdown exceeded!\n";
            g_exec.risk().setKillSwitch(true);
        }
    }
    
    std::cout << "\nShutting down...\n";
    market.disconnect();
    g_exec.stop();
    
    std::cout << "Final equity: $" << g_alloc.totalEquity() << "\n";
    std::cout << "Daily PnL: $" << g_exec.risk().dailyPnL() << "\n";
    
    return 0;
}
