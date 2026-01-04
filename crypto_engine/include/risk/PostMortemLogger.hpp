// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/risk/PostMortemLogger.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Automatic post-mortem logging for every disable event
// OWNER: Jo
// LAST VERIFIED: 2024-12-25
//
// v7.15: NEW FILE - Makes system auditable and scalable
//
// PRINCIPLE: "A 10/10 system explains itself"
// - Every disable logs full reason chain
// - Regime, slippage, correlation state captured
// - Single-screen answer to "why did we stop?"
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace Chimera {
namespace Risk {

// ─────────────────────────────────────────────────────────────────────────────
// Disable Reason Types
// ─────────────────────────────────────────────────────────────────────────────
enum class DisableReason : uint8_t {
    FAST_EXPECTANCY,       // Fast horizon went negative
    SLOW_EXPECTANCY,       // Slow horizon went negative (authority)
    PORTFOLIO_MEDIAN,      // Portfolio median expectancy negative
    REGIME_TOXIC,          // Market regime toxic
    SLIPPAGE_CRITICAL,     // Slippage exceeded threshold
    CORRELATION_LIMIT,     // Correlation group limit hit
    DAILY_LOSS_LIMIT,      // Daily loss limit hit
    MANUAL,                // Manual disable
    
    COUNT
};

inline const char* disable_reason_str(DisableReason r) noexcept {
    switch (r) {
        case DisableReason::FAST_EXPECTANCY:    return "FAST_EXPECTANCY";
        case DisableReason::SLOW_EXPECTANCY:    return "SLOW_EXPECTANCY";
        case DisableReason::PORTFOLIO_MEDIAN:   return "PORTFOLIO_MEDIAN";
        case DisableReason::REGIME_TOXIC:       return "REGIME_TOXIC";
        case DisableReason::SLIPPAGE_CRITICAL:  return "SLIPPAGE_CRITICAL";
        case DisableReason::CORRELATION_LIMIT:  return "CORRELATION_LIMIT";
        case DisableReason::DAILY_LOSS_LIMIT:   return "DAILY_LOSS_LIMIT";
        case DisableReason::MANUAL:             return "MANUAL";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Post-Mortem Event
// ─────────────────────────────────────────────────────────────────────────────
struct PostMortemEvent {
    uint64_t timestamp_ms;
    char symbol[16];
    DisableReason primary_reason;
    
    // Expectancy state at time of disable
    double fast_expectancy_bps;
    double slow_expectancy_bps;
    int fast_trades;
    int slow_trades;
    
    // Regime state
    char regime[16];           // STABLE/TRANSITION/TOXIC
    double spread_bps;
    double spread_std;
    double book_flip_rate;
    
    // Slippage state
    double realized_slippage_bps;
    double expected_slippage_bps;
    
    // Portfolio state
    double portfolio_median_expectancy;
    double portfolio_total_risk_R;
    double correlation_group_risk_R;
    
    // Daily state
    double daily_pnl_R;
    double drawdown_R;
    
    // Format as single-line CSV
    [[nodiscard]] std::string to_csv() const {
        std::ostringstream ss;
        ss << timestamp_ms << ","
           << symbol << ","
           << disable_reason_str(primary_reason) << ","
           << fast_expectancy_bps << ","
           << slow_expectancy_bps << ","
           << fast_trades << ","
           << slow_trades << ","
           << regime << ","
           << spread_bps << ","
           << spread_std << ","
           << book_flip_rate << ","
           << realized_slippage_bps << ","
           << expected_slippage_bps << ","
           << portfolio_median_expectancy << ","
           << portfolio_total_risk_R << ","
           << correlation_group_risk_R << ","
           << daily_pnl_R << ","
           << drawdown_R;
        return ss.str();
    }
    
    // Format for console display
    void print() const {
        std::cout << "\n";
        std::cout << "╔═══════════════════════════════════════════════════════════════════╗\n";
        std::cout << "║                    POST-MORTEM: SYMBOL DISABLED                   ║\n";
        std::cout << "╠═══════════════════════════════════════════════════════════════════╣\n";
        std::cout << "║ Symbol:        " << symbol << "\n";
        std::cout << "║ Time:          " << format_time(timestamp_ms) << "\n";
        std::cout << "║ PRIMARY REASON: " << disable_reason_str(primary_reason) << "\n";
        std::cout << "╠═══════════════════════════════════════════════════════════════════╣\n";
        std::cout << "║ EXPECTANCY:\n";
        std::cout << "║   Fast:  " << fast_expectancy_bps << " bps (" << fast_trades << " trades)\n";
        std::cout << "║   Slow:  " << slow_expectancy_bps << " bps (" << slow_trades << " trades)\n";
        std::cout << "╠═══════════════════════════════════════════════════════════════════╣\n";
        std::cout << "║ REGIME:\n";
        std::cout << "║   State:     " << regime << "\n";
        std::cout << "║   Spread:    " << spread_bps << " bps (std: " << spread_std << ")\n";
        std::cout << "║   Flip Rate: " << book_flip_rate << "\n";
        std::cout << "╠═══════════════════════════════════════════════════════════════════╣\n";
        std::cout << "║ SLIPPAGE:\n";
        std::cout << "║   Realized:  " << realized_slippage_bps << " bps\n";
        std::cout << "║   Expected:  " << expected_slippage_bps << " bps\n";
        std::cout << "║   Ratio:     " << (expected_slippage_bps > 0 ? realized_slippage_bps/expected_slippage_bps : 0) << "x\n";
        std::cout << "╠═══════════════════════════════════════════════════════════════════╣\n";
        std::cout << "║ PORTFOLIO:\n";
        std::cout << "║   Median E:  " << portfolio_median_expectancy << " bps\n";
        std::cout << "║   Total Risk:" << portfolio_total_risk_R << " R\n";
        std::cout << "║   Group Risk:" << correlation_group_risk_R << " R\n";
        std::cout << "╠═══════════════════════════════════════════════════════════════════╣\n";
        std::cout << "║ DAILY:\n";
        std::cout << "║   PnL:       " << daily_pnl_R << " R\n";
        std::cout << "║   Drawdown:  " << drawdown_R << " R\n";
        std::cout << "╚═══════════════════════════════════════════════════════════════════╝\n\n";
    }
    
private:
    static std::string format_time(uint64_t ts_ms) {
        time_t sec = ts_ms / 1000;
        struct tm* t = gmtime(&sec);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
        return std::string(buf) + " UTC";
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Post-Mortem Logger
// ─────────────────────────────────────────────────────────────────────────────
class PostMortemLogger {
public:
    explicit PostMortemLogger(const std::string& log_path = "postmortem.csv")
        : log_path_(log_path)
    {
        // Write header if file doesn't exist
        std::ifstream check(log_path_);
        if (!check.good()) {
            std::ofstream out(log_path_);
            out << "timestamp_ms,symbol,reason,fast_e,slow_e,fast_t,slow_t,"
                << "regime,spread,spread_std,flip_rate,slip_real,slip_exp,"
                << "port_median,port_risk,group_risk,daily_pnl,drawdown\n";
        }
    }
    
    void log(const PostMortemEvent& event) {
        // Print to console
        event.print();
        
        // Append to CSV
        std::ofstream out(log_path_, std::ios::app);
        if (out.good()) {
            out << event.to_csv() << "\n";
        }
        
        // Keep in memory for recent lookup
        recent_events_.push_back(event);
        if (recent_events_.size() > 100) {
            recent_events_.erase(recent_events_.begin());
        }
    }
    
    [[nodiscard]] const std::vector<PostMortemEvent>& recent() const noexcept {
        return recent_events_;
    }
    
    [[nodiscard]] size_t total_disables() const noexcept {
        return recent_events_.size();
    }

private:
    std::string log_path_;
    std::vector<PostMortemEvent> recent_events_;
};

} // namespace Risk
} // namespace Chimera
