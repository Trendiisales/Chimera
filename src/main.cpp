// CHIMERA v2.3.7 - PRODUCTION HARDENED
#include "BinanceRestClient.hpp"
#include "BinanceDepthWS.hpp"
#include "BinanceAggTradeWS.hpp"
#include "TradingGate.hpp"
#include "DepthGapDetector.hpp"
#include "ExecutionGateway.hpp"
#include "OrderIntent.hpp"
#include "OFIEngine.hpp"
#include "ImpulseDetector.hpp"
#include "FadeETH.hpp"

#include <iostream>
#include <fstream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <mutex>

static std::atomic<bool> running{true};
static std::atomic<uint64_t> aggtrade_count{0};
static std::atomic<uint64_t> depth_count{0};

void signal_handler(int sig) {
    std::cout << "\n[Signal " << sig << "]" << std::endl;
    running.store(false);
}

class PidLock {
    std::string pid_file;
    bool locked;
public:
    PidLock(const std::string& path) : pid_file(path), locked(false) {}
    ~PidLock() { if (locked) unlink(pid_file.c_str()); }
    bool acquire() {
        std::ifstream pf(pid_file);
        if (pf.is_open()) {
            pid_t old_pid;
            pf >> old_pid;
            pf.close();
            if (kill(old_pid, 0) == 0) {
                std::cerr << "[PidLock] Already running (PID " << old_pid << ")" << std::endl;
                return false;
            }
            unlink(pid_file.c_str());
        }
        std::ofstream out(pid_file);
        if (!out.is_open()) return false;
        out << getpid();
        locked = true;
        std::cout << "[PidLock] ✅ Locked (PID " << getpid() << ")" << std::endl;
        return true;
    }
};

class SpineServer {
    int fd;
    bool bound;
public:
    SpineServer() : fd(-1), bound(false) {}
    ~SpineServer() { if (fd >= 0) close(fd); }
    bool bind_or_die() {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return false;
        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(9001);
        if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "[Spine] FATAL: Cannot bind port 9001" << std::endl;
            return false;
        }
        listen(fd, 10);
        bound = true;
        std::cout << "[Spine] ✅ Bound to port 9001" << std::endl;
        return true;
    }
};

struct SignalState {
    std::mutex mtx;
    OFIState ofi;
    ImpulseState impulse;
};

enum Regime { NORMAL, STABLE, TRANSITION, FORCED_FLOW, INVALID };
class RegimeDetector {
    double vol_ema_ = 0.0;
    bool init_ = false;
public:
    Regime classify(double spread_bps, double vol, double ofi_z, double impulse_bps, double depth_ratio, uint64_t stall_ms) {
        // CRITICAL: Reject invalid states immediately
        if (spread_bps < 0 || spread_bps > 10.0) return INVALID;
        if (depth_ratio <= 0 || depth_ratio > 5.0) return INVALID;
        if (stall_ms > 10000) return INVALID;
        
        if (!init_) { vol_ema_ = vol; init_ = true; }
        vol_ema_ = vol_ema_ * 0.95 + vol * 0.05;
        
        // FORCED only on extreme conditions
        if (impulse_bps >= 5.0 && ofi_z >= 1.5 && depth_ratio <= 0.5) return FORCED_FLOW;
        if (spread_bps > 8.0 && vol_ema_ > 2.0) return FORCED_FLOW;
        
        if (spread_bps < 2.0 && vol_ema_ < 0.5) return STABLE;
        if (spread_bps > 4.0 || vol_ema_ > 1.0) return TRANSITION;
        return NORMAL;
    }
};

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    const char* api_key = std::getenv("BINANCE_API_KEY");
    const char* api_secret = std::getenv("BINANCE_API_SECRET");
    if (!api_key || !api_secret) {
        std::cerr << "ERROR: Set BINANCE_API_KEY and BINANCE_API_SECRET" << std::endl;
        return 1;
    }

    std::cout << "========================================" << std::endl;
    std::cout << " CHIMERA v2.3.7 - PRODUCTION HARDENED" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    mkdir("logs", 0755);
    PidLock lock("logs/supervisor.pid");
    if (!lock.acquire()) return 1;

    SpineServer spine;
    if (!spine.bind_or_die()) return 1;

    TradingGate gate;
    BinanceRestClient rest(api_key, api_secret);
    ExecutionGateway exec(gate, rest);

    FadeETH::Config fade_cfg;
    fade_cfg.notional_usd = 8000.0;
    fade_cfg.min_edge_score = 1.8;
    fade_cfg.ofi_z_minimum = 0.85;
    fade_cfg.price_impulse_min_bps = 1.2;
    fade_cfg.take_profit_bps = 2.5;
    fade_cfg.stop_loss_bps = -2.8;
    fade_cfg.micro_kill_ms = 150;
    FadeETH fade_eth(fade_cfg);

    OFIEngine ofi_engine(250, 10000);
    ImpulseDetector impulse_detector(300);
    RegimeDetector regime;
    DepthGapDetector eth_gap("ETHUSDT");
    SignalState signal_state;

    std::cout << "[INIT] Starting WebSocket streams..." << std::endl;

    BinanceAggTradeWS aggtrade("ETHUSDT", [&](const AggTrade& trade) {
        aggtrade_count++;
        if (aggtrade_count % 10 == 0) {
            std::cout << "[AGGTRADE #" << aggtrade_count << "] price=" << trade.price 
                      << " qty=" << trade.qty << std::endl;
        }
        
        uint64_t now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
        ofi_engine.onTrade(trade.qty, trade.is_buyer_maker, trade.trade_time);
        impulse_detector.onPrice(trade.price, trade.trade_time);
        {
            std::lock_guard<std::mutex> lock(signal_state.mtx);
            signal_state.ofi = ofi_engine.compute(now_ns);
            signal_state.impulse = impulse_detector.compute(now_ns);
        }
    });
    
    std::cout << "[INIT] Starting AggTrade..." << std::endl;
    aggtrade.start();
    std::this_thread::sleep_for(std::chrono::seconds(3));

    BinanceDepthWS eth_depth("ethusdt", [&](const BinanceDepthWS::DepthUpdate& d) {
        depth_count++;
        
        if (depth_count % 10 == 0) {
            std::cout << "[DEPTH #" << depth_count << "] bid=" << d.best_bid 
                      << " ask=" << d.best_ask << std::endl;
        }
        
        uint64_t now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
        
        std::string reason;
        if (!eth_gap.on_update(d.first_update_id, d.final_update_id, reason)) {
            gate.disable(reason);
        } else if (!gate.is_enabled()) {
            gate.enable("OK");
        }
        
        double mid = (d.best_bid + d.best_ask) * 0.5;
        double spread_bps = ((d.best_ask - d.best_bid) / mid) * 10000.0;
        
        // FIXED: Depth ratio is SIZE comparison only (not notional)
        double depth_ratio = d.best_bid_qty / (d.best_ask_qty + 0.001);
        
        double vol = std::abs(mid - d.best_bid);
        
        OFIState ofi_state;
        ImpulseState impulse_state;
        {
            std::lock_guard<std::mutex> lock(signal_state.mtx);
            ofi_state = signal_state.ofi;
            impulse_state = signal_state.impulse;
        }
        
        Regime current_regime = regime.classify(spread_bps, vol, ofi_state.z_score, 
                                                impulse_state.impulse_bps, depth_ratio, 
                                                impulse_state.stall_ms);
        const char* regime_str[] = {"NORMAL", "STABLE", "TRANSITION", "FORCED", "INVALID"};
        
        if (depth_count % 50 == 0) {
            std::cout << "\n[TICK " << depth_count << "] Spread=" << spread_bps 
                      << "bps Regime=" << regime_str[current_regime] << std::endl;
            std::cout << "  [OFI] z=" << ofi_state.z_score 
                      << " accel=" << ofi_state.accel << " raw=" << ofi_state.raw_ofi << std::endl;
            std::cout << "  [IMPULSE] " << impulse_state.impulse_bps << "bps"
                      << " velocity=" << impulse_state.velocity_bps_per_sec
                      << " stall=" << impulse_state.stall_ms << "ms" << std::endl;
            std::cout << "  Depth ratio=" << depth_ratio << "\n" << std::endl;
        }
        
        if (gate.is_enabled() && current_regime == NORMAL && ofi_state.z_score >= 0.85 && impulse_state.impulse_bps >= 1.2) {
            auto intent = fade_eth.onDepth(
                d.best_bid, d.best_ask,
                d.best_bid_qty * d.best_bid, d.best_ask_qty * d.best_ask,
                ofi_state.z_score, ofi_state.accel, impulse_state.impulse_bps,
                0.0, 0.0, 0.0, now_ns
            );
            if (intent.has_value()) {
                std::string result;
                exec.send(intent.value(), result);
                std::cout << "[SIGNAL] Edge conditions met! OFI=" << ofi_state.z_score 
                          << " Impulse=" << impulse_state.impulse_bps << std::endl;
            }
        }
    });
    
    std::cout << "[INIT] Starting Depth..." << std::endl;
    eth_depth.start();
    std::this_thread::sleep_for(std::chrono::seconds(3));

    std::cout << "\n✅ ONLINE - Waiting for signals..." << std::endl;

    auto last_check = std::chrono::steady_clock::now();
    while (running.load()) {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_check).count() >= 5) {
            std::cout << "[STATUS] AggTrades: " << aggtrade_count.load() 
                      << " | Depth updates: " << depth_count.load() << std::endl;
            last_check = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    eth_depth.stop();
    aggtrade.stop();
    std::cout << "\nFinal counts: AggTrades=" << aggtrade_count.load() 
              << " Depth=" << depth_count.load() << std::endl;
    return 0;
}
