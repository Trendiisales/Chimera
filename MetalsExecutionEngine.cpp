#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sched.h>
#include <thread>
#include <unistd.h>

using Clock = std::chrono::steady_clock;
using ns    = std::chrono::nanoseconds;

// === IMPULSE THRESHOLDS (EXTRACTION OPTIMIZED) ===
static constexpr double XAU_IMPULSE_SOFT  = 0.10;  // Drift floor (was blocking valid trades)
static constexpr double XAU_IMPULSE_MED   = 0.15;  // Medium impulse (lowered from 0.18)
static constexpr double XAU_IMPULSE_HARD  = 0.26;  // Strong impulse (lowered from 0.30)

static constexpr double XAG_IMPULSE_SOFT  = 0.015; // Conservative for silver
static constexpr double XAG_IMPULSE_MED   = 0.04;
static constexpr double XAG_IMPULSE_HARD  = 0.07;

// === SIZE SCALING (IMPULSE-WEIGHTED) ===
static constexpr double XAU_DRIFT_SIZE    = 0.70;  // Drift entries
static constexpr double XAU_BASE_SIZE     = 1.00;  // Medium impulse
static constexpr double XAU_STRONG_SIZE   = 1.25;  // Strong impulse
static constexpr double XAU_EXTREME_SIZE  = 1.50;  // Extreme impulse + FAST latency

static constexpr double XAG_DRIFT_SIZE    = 0.75;
static constexpr double XAG_BASE_SIZE     = 1.00;
static constexpr double XAG_STRONG_SIZE   = 1.20;

// === TWO-TIER COOLDOWN (CRITICAL FIX) ===
static constexpr ns HARD_COOLDOWN   = ns(400'000'000);   // 400ms (SL, failures)
static constexpr ns SOFT_COOLDOWN   = ns(800'000'000);   // 800ms (was 1500ms)

// === RISK GOVERNOR (PNL LADDER) ===
static constexpr double MAX_DAILY_LOSS   = -1.5;  // HALT
static constexpr double REDUCE_AT_LOSS_1 = -1.0;  // 0.50x size
static constexpr double REDUCE_AT_LOSS_2 = -0.5;  // 0.75x size
static constexpr double LOCK_PROFIT_AT   = +5.0;  // Lock in profits

// === IMPULSE DECAY (RUNNER PROTECTION) ===
static constexpr double DECAY_WARN_RATIO = 0.48;  // Warn (was 0.55)
static constexpr double DECAY_EXIT_RATIO = 0.30;  // Force exit (was 0.35)

struct SymbolState {
    double last_price = 0.0;
    double velocity   = 0.0;
    double entry_impulse = 0.0;
    double pnl        = 0.0;

    Clock::time_point last_trade;
    Clock::time_point entry_time;
    bool in_trade = false;
    bool hard_cooldown = false;  // Track cooldown type
    int consecutive_losses = 0;
};

struct RiskGovernor {
    std::atomic<double> day_pnl{0.0};
    std::atomic<bool> halted{false};
    std::atomic<bool> profit_locked{false};

    double size_multiplier() {
        double pnl = day_pnl.load();
        
        // HALT on max loss
        if (pnl <= MAX_DAILY_LOSS) {
            halted.store(true);
            return 0.0;
        }
        
        // Lock profits
        if (pnl >= LOCK_PROFIT_AT) {
            profit_locked.store(true);
            return 0.0;  // No new entries
        }
        
        // PnL ladder
        if (pnl <= REDUCE_AT_LOSS_1) return 0.50;
        if (pnl <= REDUCE_AT_LOSS_2) return 0.75;
        
        return 1.0;
    }
};

RiskGovernor governor;

// === CPU PINNING (XAU/XAG ISOLATION) ===
static inline void pin_thread(int cpu) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

double compute_velocity(double prev, double now) {
    return now - prev;
}

bool latency_fast() {
    // In real system, fed by LatencyMonitor
    // p95 <= 7.0ms = FAST
    return true;
}

// === IMPULSE-WEIGHTED SIZE DECISION ===
double decide_size(const char* sym, double impulse, bool latency_is_fast) {
    double abs_imp = fabs(impulse);
    
    if (strcmp(sym, "XAU") == 0) {
        // Drift entry (new alpha source)
        if (abs_imp >= XAU_IMPULSE_SOFT && abs_imp < XAU_IMPULSE_MED) {
            return latency_is_fast ? XAU_DRIFT_SIZE : 0.0;
        }
        
        // Medium impulse
        if (abs_imp >= XAU_IMPULSE_MED && abs_imp < XAU_IMPULSE_HARD) {
            return XAU_BASE_SIZE;
        }
        
        // Strong impulse
        if (abs_imp >= XAU_IMPULSE_HARD) {
            return latency_is_fast ? XAU_EXTREME_SIZE : XAU_STRONG_SIZE;
        }
    } 
    else { // XAG
        if (abs_imp >= XAG_IMPULSE_SOFT && abs_imp < XAG_IMPULSE_MED) {
            return XAG_DRIFT_SIZE;
        }
        if (abs_imp >= XAG_IMPULSE_MED && abs_imp < XAG_IMPULSE_HARD) {
            return XAG_BASE_SIZE;
        }
        if (abs_imp >= XAG_IMPULSE_HARD) {
            return XAG_STRONG_SIZE;
        }
    }
    
    return 0.0;
}

// === TWO-TIER COOLDOWN CHECK ===
bool in_cooldown(SymbolState& s) {
    auto now = Clock::now();
    auto dt  = now - s.last_trade;
    
    // Hard cooldown (after SL or failures)
    if (s.hard_cooldown && dt < HARD_COOLDOWN) return true;
    
    // Soft cooldown (normal)
    if (dt < SOFT_COOLDOWN) return true;
    
    return false;
}

// === IMPULSE DECAY CHECK (RUNNER PROTECTION) ===
double compute_effective_impulse(double entry_impulse, Clock::time_point entry_time) {
    auto now = Clock::now();
    auto dt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - entry_time).count();
    
    // Exponential decay: exp(-dt / 120ms)
    double decay_factor = std::exp(-dt_ms / 120.0);
    return entry_impulse * decay_factor;
}

void execution_loop(const char* sym, int cpu) {
    pin_thread(cpu);
    std::cout << "[" << sym << "] Pinned to CPU " << cpu << "\n";

    SymbolState state{};
    double prev_price = 0.0;

    while (!governor.halted.load() && !governor.profit_locked.load()) {
        // === MARKET DATA FEED (stub) ===
        double price = state.last_price + ((rand() % 100) - 50) * 0.01;
        state.velocity = compute_velocity(prev_price, price);
        prev_price = price;
        state.last_price = price;

        double current_impulse = state.velocity;
        bool lat_fast = latency_fast();

        // === ENTRY LOGIC ===
        if (!state.in_trade && !in_cooldown(state)) {
            double size = decide_size(sym, current_impulse, lat_fast);
            size *= governor.size_multiplier();  // PnL ladder

            if (size > 0.0) {
                state.in_trade = true;
                state.entry_time = Clock::now();
                state.last_trade = Clock::now();
                state.entry_impulse = fabs(current_impulse);
                state.hard_cooldown = false;  // Reset to soft

                double trade_pnl = current_impulse * size * 0.5;  // Simplified PnL
                state.pnl += trade_pnl;
                governor.day_pnl.fetch_add(trade_pnl);

                std::cout
                    << "[" << sym << "] ENTRY"
                    << " size=" << size
                    << " impulse=" << current_impulse
                    << " (entry_impulse=" << state.entry_impulse << ")"
                    << " day_pnl=" << governor.day_pnl.load()
                    << "\n";
            }
        }

        // === EXIT LOGIC (IMPULSE DECAY) ===
        if (state.in_trade) {
            double effective_impulse = compute_effective_impulse(state.entry_impulse, state.entry_time);
            double decay_ratio = effective_impulse / state.entry_impulse;
            
            // Force exit if impulse collapsed
            if (decay_ratio < DECAY_EXIT_RATIO) {
                state.in_trade = false;
                
                double exit_pnl = state.velocity * 0.3;  // Simplified
                bool is_loss = (exit_pnl < 0);
                
                if (is_loss) {
                    state.consecutive_losses++;
                    if (state.consecutive_losses >= 2) {
                        state.hard_cooldown = true;  // Switch to hard cooldown
                    }
                } else {
                    state.consecutive_losses = 0;  // Reset on win
                }
                
                std::cout 
                    << "[" << sym << "] EXIT DECAY"
                    << " decay_ratio=" << decay_ratio
                    << " pnl=" << exit_pnl
                    << " consecutive_losses=" << state.consecutive_losses
                    << " cooldown=" << (state.hard_cooldown ? "HARD" : "SOFT")
                    << "\n";
            }
            // Warn if decaying
            else if (decay_ratio < DECAY_WARN_RATIO) {
                std::cout 
                    << "[" << sym << "] DECAY_WARN decay_ratio=" << decay_ratio << "\n";
            }
        }

        std::this_thread::sleep_for(ns(2'000'000)); // 2ms tick
    }

    if (governor.halted.load()) {
        std::cout << "[" << sym << "] HALTED (max loss)\n";
    }
    if (governor.profit_locked.load()) {
        std::cout << "[" << sym << "] PROFIT LOCKED (+" 
                  << governor.day_pnl.load() << ")\n";
    }
}

int main() {
    std::cout << "===========================================\n";
    std::cout << "Metals Execution Engine - EXTRACTION MODE\n";
    std::cout << "===========================================\n\n";
    
    std::cout << "Configuration:\n";
    std::cout << "  XAU impulse: SOFT=" << XAU_IMPULSE_SOFT 
              << " MED=" << XAU_IMPULSE_MED 
              << " HARD=" << XAU_IMPULSE_HARD << "\n";
    std::cout << "  XAU sizing: DRIFT=" << XAU_DRIFT_SIZE 
              << " BASE=" << XAU_BASE_SIZE 
              << " STRONG=" << XAU_STRONG_SIZE 
              << " EXTREME=" << XAU_EXTREME_SIZE << "\n";
    std::cout << "  Cooldown: HARD=" << HARD_COOLDOWN.count()/1e6 << "ms"
              << " SOFT=" << SOFT_COOLDOWN.count()/1e6 << "ms\n";
    std::cout << "  PnL ladder: -1.5 HALT, -1.0 → 0.5x, -0.5 → 0.75x, +5.0 LOCK\n";
    std::cout << "  CPU isolation: XAU=CPU2, XAG=CPU3\n\n";

    std::thread xau([](){ execution_loop("XAU", 2); });
    std::thread xag([](){ execution_loop("XAG", 3); });

    xau.join();
    xag.join();

    return 0;
}
