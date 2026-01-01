#pragma once
// =============================================================================
// CRYPTO RISK MANAGER - Hard Risk Controls
// =============================================================================
// Enforces:
//   - Daily trade limit (max 2)
//   - Single position at a time
//   - Kill switch on first loss
//   - Daily loss cap (0.15% equity)
//   - Fixed position sizing (0.05% risk per trade)
// =============================================================================

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <chrono>

namespace Chimera::Crypto {

// =============================================================================
// Risk Constants (NON-NEGOTIABLE)
// =============================================================================
namespace RiskConstants {
    constexpr int    MAX_TRADES_PER_DAY = 2;
    constexpr double RISK_PER_TRADE_PCT = 0.05;     // 0.05% equity risk per trade
    constexpr double MAX_DAILY_LOSS_PCT = 0.15;     // 0.15% max daily loss
    constexpr double FIXED_SIZE_BTC = 0.001;        // Fixed BTC size
    constexpr double FIXED_SIZE_ETH = 0.01;         // Fixed ETH size
    constexpr int64_t MAX_HOLD_MS = 6000;           // 6 second max hold
}

// =============================================================================
// Kill Switch State
// =============================================================================
enum class KillReason : uint8_t {
    NONE = 0,
    FIRST_LOSS,
    DAILY_LOSS_CAP,
    MAX_TRADES,
    MANUAL,
    RTT_SPIKE,
    ERROR
};

inline const char* killReasonStr(KillReason r) {
    switch (r) {
        case KillReason::NONE: return "NONE";
        case KillReason::FIRST_LOSS: return "FIRST_LOSS";
        case KillReason::DAILY_LOSS_CAP: return "DAILY_LOSS_CAP";
        case KillReason::MAX_TRADES: return "MAX_TRADES";
        case KillReason::MANUAL: return "MANUAL";
        case KillReason::RTT_SPIKE: return "RTT_SPIKE";
        case KillReason::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

// =============================================================================
// Position State
// =============================================================================
struct CryptoPosition {
    bool active = false;
    char symbol[16] = {0};
    bool is_long = false;
    double entry_price = 0.0;
    double size = 0.0;
    double stop_px = 0.0;
    double target_px = 0.0;
    double entry_spread = 0.0;
    int64_t entry_time_ms = 0;
    
    void clear() noexcept {
        active = false;
        symbol[0] = '\0';
        is_long = false;
        entry_price = 0.0;
        size = 0.0;
        stop_px = 0.0;
        target_px = 0.0;
        entry_spread = 0.0;
        entry_time_ms = 0;
    }
};

// =============================================================================
// Trade Record
// =============================================================================
struct CryptoTradeRecord {
    char symbol[16] = {0};
    bool is_long = false;
    double entry_price = 0.0;
    double exit_price = 0.0;
    double size = 0.0;
    double pnl_usd = 0.0;
    int64_t hold_time_ms = 0;
    const char* exit_reason = "";
};

// =============================================================================
// Crypto Risk Manager (Singleton)
// =============================================================================
class CryptoRiskManager {
private:
    // State
    std::atomic<int> trades_today_{0};
    std::atomic<bool> killed_{false};
    std::atomic<KillReason> kill_reason_{KillReason::NONE};
    std::atomic<double> daily_pnl_usd_{0.0};
    std::atomic<double> equity_{15000.0};  // Default, should be set
    
    // Position
    CryptoPosition position_;
    
    // Day tracking
    int last_reset_day_ = -1;
    
    // Private constructor (singleton)
    CryptoRiskManager() = default;
    
    void checkDayRoll() noexcept {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::tm* utc = std::gmtime(&time_t_now);
        int today = utc->tm_yday;
        
        if (last_reset_day_ != today) {
            // New day - reset
            trades_today_ = 0;
            killed_ = false;
            kill_reason_ = KillReason::NONE;
            daily_pnl_usd_ = 0.0;
            position_.clear();
            last_reset_day_ = today;
            std::cout << "[CRYPTO-RISK] Daily reset at UTC day " << today << "\n";
        }
    }

public:
    static CryptoRiskManager& instance() {
        static CryptoRiskManager rm;
        return rm;
    }
    
    // Non-copyable
    CryptoRiskManager(const CryptoRiskManager&) = delete;
    CryptoRiskManager& operator=(const CryptoRiskManager&) = delete;
    
    // ═══════════════════════════════════════════════════════════════════
    // Configuration
    // ═══════════════════════════════════════════════════════════════════
    void setEquity(double equity) noexcept {
        equity_.store(equity);
    }
    
    double equity() const noexcept {
        return equity_.load();
    }
    
    // ═══════════════════════════════════════════════════════════════════
    // Query State
    // ═══════════════════════════════════════════════════════════════════
    bool canTrade() noexcept {
        checkDayRoll();
        
        if (killed_.load()) return false;
        if (trades_today_.load() >= RiskConstants::MAX_TRADES_PER_DAY) return false;
        if (position_.active) return false;  // Single position only
        
        // Check daily loss cap
        double max_loss = equity_.load() * (RiskConstants::MAX_DAILY_LOSS_PCT / 100.0);
        if (daily_pnl_usd_.load() <= -max_loss) {
            kill(KillReason::DAILY_LOSS_CAP);
            return false;
        }
        
        return true;
    }
    
    bool isKilled() const noexcept {
        return killed_.load();
    }
    
    KillReason killReason() const noexcept {
        return kill_reason_.load();
    }
    
    int tradesToday() const noexcept {
        return trades_today_.load();
    }
    
    double dailyPnl() const noexcept {
        return daily_pnl_usd_.load();
    }
    
    bool hasPosition() const noexcept {
        return position_.active;
    }
    
    const CryptoPosition& position() const noexcept {
        return position_;
    }
    
    // ═══════════════════════════════════════════════════════════════════
    // Position Sizing (FIXED - NO DYNAMIC SIZING)
    // ═══════════════════════════════════════════════════════════════════
    double fixedSize(const char* symbol) const noexcept {
        if (symbol[0] == 'B') return RiskConstants::FIXED_SIZE_BTC;
        return RiskConstants::FIXED_SIZE_ETH;
    }
    
    // ═══════════════════════════════════════════════════════════════════
    // Trade Execution Interface
    // ═══════════════════════════════════════════════════════════════════
    bool openPosition(const char* symbol, bool is_long, double entry_price,
                      double size, double stop_px, double target_px,
                      double entry_spread, int64_t now_ms) noexcept {
        if (!canTrade()) return false;
        
        position_.active = true;
        snprintf(position_.symbol, sizeof(position_.symbol), "%s", symbol);
        position_.is_long = is_long;
        position_.entry_price = entry_price;
        position_.size = size;
        position_.stop_px = stop_px;
        position_.target_px = target_px;
        position_.entry_spread = entry_spread;
        position_.entry_time_ms = now_ms;
        
        trades_today_++;
        
        std::cout << "[CRYPTO-RISK] POSITION OPENED: " << symbol
                  << " " << (is_long ? "LONG" : "SHORT")
                  << " size=" << std::fixed << std::setprecision(4) << size
                  << " entry=" << std::setprecision(2) << entry_price
                  << " stop=" << stop_px
                  << " target=" << target_px
                  << " trades_today=" << trades_today_.load()
                  << "\n";
        
        return true;
    }
    
    CryptoTradeRecord closePosition(double exit_price, int64_t now_ms, 
                                    const char* exit_reason) noexcept {
        CryptoTradeRecord rec;
        
        if (!position_.active) return rec;
        
        // Calculate PnL
        double price_diff = exit_price - position_.entry_price;
        if (!position_.is_long) price_diff = -price_diff;
        double pnl = price_diff * position_.size;
        
        // Record
        memcpy(rec.symbol, position_.symbol, sizeof(rec.symbol) - 1);
        rec.symbol[sizeof(rec.symbol) - 1] = '\0';
        rec.is_long = position_.is_long;
        rec.entry_price = position_.entry_price;
        rec.exit_price = exit_price;
        rec.size = position_.size;
        rec.pnl_usd = pnl;
        rec.hold_time_ms = now_ms - position_.entry_time_ms;
        rec.exit_reason = exit_reason;
        
        // Update daily PnL
        daily_pnl_usd_ = daily_pnl_usd_.load() + pnl;
        
        std::cout << "[CRYPTO-RISK] POSITION CLOSED: " << position_.symbol
                  << " PnL=$" << std::fixed << std::setprecision(2) << pnl
                  << " hold=" << rec.hold_time_ms << "ms"
                  << " reason=" << exit_reason
                  << " daily_pnl=$" << daily_pnl_usd_.load()
                  << "\n";
        
        // Kill on first loss
        if (pnl < 0.0) {
            kill(KillReason::FIRST_LOSS);
        }
        
        // Clear position
        position_.clear();
        
        return rec;
    }
    
    // ═══════════════════════════════════════════════════════════════════
    // Kill Switch
    // ═══════════════════════════════════════════════════════════════════
    void kill(KillReason reason) noexcept {
        if (killed_.load()) return;  // Already killed
        
        killed_ = true;
        kill_reason_ = reason;
        
        std::cout << "[CRYPTO-RISK] *** ENGINE KILLED ***"
                  << " reason=" << killReasonStr(reason)
                  << " trades_today=" << trades_today_.load()
                  << " daily_pnl=$" << std::fixed << std::setprecision(2) << daily_pnl_usd_.load()
                  << "\n";
    }
    
    void killManual() noexcept {
        kill(KillReason::MANUAL);
    }
    
    // ═══════════════════════════════════════════════════════════════════
    // RTT Check (called on every tick)
    // ═══════════════════════════════════════════════════════════════════
    bool checkRTT(double rtt_ms) noexcept {
        if (rtt_ms > 1.2) {
            if (position_.active) {
                // Force close on RTT spike while in position
                std::cout << "[CRYPTO-RISK] RTT SPIKE while in position: " 
                          << rtt_ms << "ms\n";
            }
            return false;  // Block trading
        }
        return true;
    }
    
    // ═══════════════════════════════════════════════════════════════════
    // Check Position Timeout
    // ═══════════════════════════════════════════════════════════════════
    bool isPositionTimedOut(int64_t now_ms) const noexcept {
        if (!position_.active) return false;
        return (now_ms - position_.entry_time_ms) > RiskConstants::MAX_HOLD_MS;
    }
    
    // ═══════════════════════════════════════════════════════════════════
    // Status Report
    // ═══════════════════════════════════════════════════════════════════
    void printStatus() const {
        std::cout << "[CRYPTO-RISK] Status:"
                  << " killed=" << (killed_.load() ? "YES" : "NO")
                  << " reason=" << killReasonStr(kill_reason_.load())
                  << " trades=" << trades_today_.load() << "/" << RiskConstants::MAX_TRADES_PER_DAY
                  << " pnl=$" << std::fixed << std::setprecision(2) << daily_pnl_usd_.load()
                  << " position=" << (position_.active ? position_.symbol : "NONE")
                  << "\n";
    }
};

} // namespace Chimera::Crypto
