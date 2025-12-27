// ═══════════════════════════════════════════════════════════════════════════════
// Alpha Trading System - Internal GUI Cockpit
// ═══════════════════════════════════════════════════════════════════════════════
// VERSION: 1.2.0
// PURPOSE: Real-time trading dashboard for XAUUSD and NAS100
//
// PANELS:
// - System Status (uptime, connection state, kill switch)
// - XAUUSD Engine (prices, regime, position, P&L)
// - NAS100 Engine (prices, regime, position, P&L)
// - Trade History (recent trades with attribution)
// - ML Features (live feature values, prediction confidence)
// - Session Info (current session, multipliers, gates)
// - Risk Monitor (exposure, drawdown, expectancy)
// - Event Log (real-time event stream)
//
// FEATURES:
// - Dear ImGui based (lightweight, immediate mode)
// - Non-blocking (runs in separate thread or main loop)
// - Configurable layout and colors
// - Screenshot and recording support
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include "../core/Types.hpp"
#include "../session/SessionDetector.hpp"
#include "../exit/ExitLogic.hpp"
#include "../ml/MLFeatures.hpp"
#include "../logging/TradeLogger.hpp"
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace Alpha {
namespace GUI {

// ═══════════════════════════════════════════════════════════════════════════════
// COLOR PALETTE (Trading Terminal Style)
// ═══════════════════════════════════════════════════════════════════════════════
struct Colors {
    // Backgrounds
    static constexpr uint32_t BG_DARK = 0xFF1A1A1F;
    static constexpr uint32_t BG_PANEL = 0xFF252530;
    static constexpr uint32_t BG_HEADER = 0xFF303040;
    
    // Text
    static constexpr uint32_t TEXT = 0xFFE0E0E0;
    static constexpr uint32_t TEXT_DIM = 0xFF808090;
    static constexpr uint32_t TEXT_BRIGHT = 0xFFFFFFFF;
    
    // Status
    static constexpr uint32_t GREEN = 0xFF00DD00;
    static constexpr uint32_t RED = 0xFFDD0000;
    static constexpr uint32_t YELLOW = 0xFFDDDD00;
    static constexpr uint32_t BLUE = 0xFF4080FF;
    static constexpr uint32_t ORANGE = 0xFFFF8000;
    static constexpr uint32_t CYAN = 0xFF00DDDD;
    static constexpr uint32_t MAGENTA = 0xFFDD00DD;
    
    // Instruments
    static constexpr uint32_t GOLD = 0xFFFFD700;
    static constexpr uint32_t NAS = 0xFF00AAFF;
    
    // Sessions
    static constexpr uint32_t ASIA = 0xFFFF6B6B;
    static constexpr uint32_t LONDON = 0xFF4ECDC4;
    static constexpr uint32_t NY = 0xFFFFE66D;
    
    static uint32_t for_pnl(double pnl) {
        if (pnl > 0.5) return GREEN;
        if (pnl < -0.5) return RED;
        return TEXT_DIM;
    }
    
    static uint32_t for_session(SessionType s) {
        switch (s) {
            case SessionType::ASIA: return ASIA;
            case SessionType::LONDON_OPEN:
            case SessionType::LONDON_PM: return LONDON;
            case SessionType::CASH_OPEN:
            case SessionType::NY_AFTERNOON:
            case SessionType::US_DATA:
            case SessionType::POWER_HOUR: return NY;
            default: return TEXT_DIM;
        }
    }
    
    static uint32_t for_instrument(Instrument i) {
        return (i == Instrument::XAUUSD) ? GOLD : NAS;
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// ENGINE STATUS DATA
// ═══════════════════════════════════════════════════════════════════════════════
struct EngineDisplayData {
    Instrument instrument = Instrument::INVALID;
    EngineState state = EngineState::INIT;
    
    // Prices
    double bid = 0.0;
    double ask = 0.0;
    double spread = 0.0;
    double spread_bps = 0.0;
    
    // Stats
    uint64_t tick_count = 0;
    int trade_count = 0;
    double pnl_bps = 0.0;
    double daily_pnl_bps = 0.0;
    
    // Regime
    std::string regime = "QUIET";
    double momentum_fast = 0.0;
    double momentum_slow = 0.0;
    double volatility = 0.0;
    double atr_ratio = 1.0;
    
    // Session
    SessionType session = SessionType::OFF;
    double session_multiplier = 0.0;
    bool is_peak = false;
    
    // Position
    bool has_position = false;
    int position_side = 0;
    double position_entry = 0.0;
    double position_size = 0.0;
    double position_r = 0.0;
    uint64_t position_hold_ms = 0;
    bool position_risk_free = false;
    bool position_scaled = false;
    int stop_moves = 0;
    
    // Expectancy
    double expectancy = 0.0;
    double win_rate = 0.0;
    int total_trades = 0;
};

// ═══════════════════════════════════════════════════════════════════════════════
// TRADE DISPLAY RECORD
// ═══════════════════════════════════════════════════════════════════════════════
struct TradeDisplayRecord {
    uint64_t timestamp = 0;
    Instrument instrument = Instrument::INVALID;
    int side = 0;
    double entry = 0.0;
    double exit_price = 0.0;
    double pnl_bps = 0.0;
    double r_multiple = 0.0;
    uint64_t hold_ms = 0;
    std::string exit_reason;
    SessionType session = SessionType::OFF;
};

// ═══════════════════════════════════════════════════════════════════════════════
// EVENT DISPLAY RECORD
// ═══════════════════════════════════════════════════════════════════════════════
struct EventDisplayRecord {
    uint64_t timestamp = 0;
    Logging::LogLevel level = Logging::LogLevel::INFO;
    std::string category;
    std::string message;
    Instrument instrument = Instrument::INVALID;
};

// ═══════════════════════════════════════════════════════════════════════════════
// SYSTEM STATUS DATA
// ═══════════════════════════════════════════════════════════════════════════════
struct SystemDisplayData {
    // Connection
    bool fix_quote_connected = false;
    bool fix_trade_connected = false;
    std::string fix_state = "DISCONNECTED";
    
    // Timing
    uint64_t uptime_ms = 0;
    uint64_t start_time = 0;
    
    // Mode
    bool shadow_mode = true;
    bool kill_switch_active = false;
    std::string kill_reason;
    
    // Stats
    uint64_t total_ticks = 0;
    int total_trades = 0;
    double total_pnl_bps = 0.0;
    int daily_trades = 0;
    double daily_pnl_bps = 0.0;
    
    // Dashboard
    int dashboard_clients = 0;
    
    // ML
    uint64_t ml_features_logged = 0;
    uint64_t ml_trades_logged = 0;
    bool ml_enabled = false;
};

// ═══════════════════════════════════════════════════════════════════════════════
// GUI STATE
// ═══════════════════════════════════════════════════════════════════════════════
class AlphaGUI {
public:
    AlphaGUI() noexcept = default;
    
    // ─────────────────────────────────────────────────────────────────────────
    // DATA UPDATE (called from engine thread)
    // ─────────────────────────────────────────────────────────────────────────
    void update_system(const SystemDisplayData& data) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        system_ = data;
    }
    
    void update_engine(const EngineDisplayData& data) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (data.instrument == Instrument::XAUUSD) {
            gold_ = data;
        } else if (data.instrument == Instrument::NAS100) {
            nas_ = data;
        }
    }
    
    void add_trade(const TradeDisplayRecord& trade) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        trades_.push_front(trade);
        if (trades_.size() > 100) trades_.pop_back();
    }
    
    void add_event(const EventDisplayRecord& event) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_front(event);
        if (events_.size() > 500) events_.pop_back();
    }
    
    void update_ml_features(Instrument inst, const ML::FeatureVector& fv) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (inst == Instrument::XAUUSD) {
            gold_features_ = fv;
        } else {
            nas_features_ = fv;
        }
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // RENDER (called from GUI thread - generates string output for now)
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] std::string render_text() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream ss;
        
        render_header(ss);
        render_system_panel(ss);
        render_engine_panel(ss, gold_);
        render_engine_panel(ss, nas_);
        render_trades_panel(ss);
        render_footer(ss);
        
        return ss.str();
    }
    
    // For future ImGui integration
    void render_imgui() noexcept {
        // TODO: Implement Dear ImGui rendering
        // This will be called from the GUI thread with ImGui context active
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // CONTROLS
    // ─────────────────────────────────────────────────────────────────────────
    void request_kill_switch() noexcept {
        kill_requested_.store(true);
    }
    
    [[nodiscard]] bool kill_requested() const noexcept {
        return kill_requested_.exchange(false);
    }
    
    void toggle_shadow_mode() noexcept {
        shadow_toggle_requested_.store(true);
    }
    
    [[nodiscard]] bool shadow_toggle_requested() const noexcept {
        return shadow_toggle_requested_.exchange(false);
    }

private:
    void render_header(std::ostringstream& ss) const noexcept {
        ss << "\033[2J\033[H";  // Clear screen
        ss << "╔══════════════════════════════════════════════════════════════════════════════╗\n";
        ss << "║  ALPHA TRADING SYSTEM v1.2.0                                                 ║\n";
        ss << "║  INSTRUMENTS: XAUUSD • NAS100         MODE: " 
           << (system_.shadow_mode ? "SHADOW    " : "LIVE      ")
           << "                      ║\n";
        ss << "╠══════════════════════════════════════════════════════════════════════════════╣\n";
    }
    
    void render_system_panel(std::ostringstream& ss) const noexcept {
        // Format uptime
        uint64_t secs = system_.uptime_ms / 1000;
        uint64_t hrs = secs / 3600;
        uint64_t mins = (secs % 3600) / 60;
        uint64_t s = secs % 60;
        
        ss << "║  SYSTEM                                                                      ║\n";
        ss << "║  Uptime: " << std::setw(2) << std::setfill('0') << hrs << ":" 
           << std::setw(2) << mins << ":" << std::setw(2) << s << std::setfill(' ')
           << "    FIX: " << (system_.fix_quote_connected ? "✓ QUOTE" : "✗ QUOTE")
           << " " << (system_.fix_trade_connected ? "✓ TRADE" : "✗ TRADE");
        
        if (system_.kill_switch_active) {
            ss << "    ⚠ KILL: " << system_.kill_reason;
        }
        ss << std::string(80 - 75, ' ') << "║\n";
        
        ss << "║  Ticks: " << std::setw(10) << system_.total_ticks
           << "    Trades: " << std::setw(4) << system_.total_trades
           << "    PnL: " << std::fixed << std::setprecision(1) << std::setw(8) 
           << system_.total_pnl_bps << " bps";
        ss << std::string(80 - 68, ' ') << "║\n";
        ss << "╠──────────────────────────────────────────────────────────────────────────────╣\n";
    }
    
    void render_engine_panel(std::ostringstream& ss, const EngineDisplayData& eng) const noexcept {
        const char* name = instrument_str(eng.instrument);
        
        ss << "║  " << std::setw(6) << name 
           << "  [" << std::setw(7) << engine_state_str(eng.state) << "]"
           << "  Session: " << std::setw(6) << session_type_str(eng.session)
           << "  Regime: " << std::setw(8) << eng.regime;
        ss << std::string(80 - 65, ' ') << "║\n";
        
        ss << std::fixed << std::setprecision(2);
        ss << "║  Bid: " << std::setw(10) << eng.bid
           << "  Ask: " << std::setw(10) << eng.ask
           << "  Spread: " << std::setw(5) << eng.spread_bps << " bps"
           << "  ATR: " << std::setw(4) << eng.atr_ratio << "x";
        ss << std::string(80 - 72, ' ') << "║\n";
        
        if (eng.has_position) {
            ss << "║  POSITION: " << (eng.position_side > 0 ? "LONG " : "SHORT")
               << " @ " << std::setw(10) << eng.position_entry
               << "  R=" << std::setw(6) << std::setprecision(2) << eng.position_r
               << "  Hold=" << std::setw(6) << (eng.position_hold_ms / 1000) << "s";
            if (eng.position_risk_free) ss << " [BE]";
            if (eng.position_scaled) ss << " [SCALED]";
            ss << std::string(80 - 75, ' ') << "║\n";
        } else {
            ss << "║  POSITION: FLAT"
               << "    Expectancy: " << std::setprecision(3) << eng.expectancy
               << "    Win Rate: " << std::setprecision(1) << (eng.win_rate * 100) << "%";
            ss << std::string(80 - 60, ' ') << "║\n";
        }
        
        ss << "║  Ticks: " << std::setw(8) << eng.tick_count
           << "  Trades: " << std::setw(4) << eng.trade_count
           << "  PnL: " << std::setprecision(1) << std::setw(8) << eng.pnl_bps << " bps";
        ss << std::string(80 - 56, ' ') << "║\n";
        ss << "╠──────────────────────────────────────────────────────────────────────────────╣\n";
    }
    
    void render_trades_panel(std::ostringstream& ss) const noexcept {
        ss << "║  RECENT TRADES                                                               ║\n";
        
        int count = 0;
        for (const auto& trade : trades_) {
            if (count++ >= 5) break;
            
            ss << std::fixed << std::setprecision(2);
            ss << "║  " << std::setw(6) << instrument_str(trade.instrument)
               << " " << (trade.side > 0 ? "L" : "S")
               << " " << std::setw(10) << trade.entry
               << " -> " << std::setw(10) << trade.exit_price
               << "  R=" << std::setw(6) << trade.r_multiple
               << "  " << std::setw(12) << trade.exit_reason;
            ss << std::string(80 - 68, ' ') << "║\n";
        }
        
        if (trades_.empty()) {
            ss << "║  (no trades yet)                                                             ║\n";
        }
    }
    
    void render_footer(std::ostringstream& ss) const noexcept {
        ss << "╠──────────────────────────────────────────────────────────────────────────────╣\n";
        
        // Current session info
        auto session = current_session(Instrument::XAUUSD);
        ss << "║  Session: " << session_str(session.session)
           << "  Multiplier: " << std::fixed << std::setprecision(1) << session.size_multiplier
           << "  Peak: " << (session.is_peak ? "YES" : "NO");
        ss << std::string(80 - 45, ' ') << "║\n";
        
        ss << "╚══════════════════════════════════════════════════════════════════════════════╝\n";
        
        // Timestamp
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        ss << "Last update: " << std::ctime(&time);
    }
    
    mutable std::mutex mutex_;
    
    SystemDisplayData system_;
    EngineDisplayData gold_;
    EngineDisplayData nas_;
    ML::FeatureVector gold_features_;
    ML::FeatureVector nas_features_;
    
    std::deque<TradeDisplayRecord> trades_;
    std::deque<EventDisplayRecord> events_;
    
    mutable std::atomic<bool> kill_requested_{false};
    mutable std::atomic<bool> shadow_toggle_requested_{false};
};

// ═══════════════════════════════════════════════════════════════════════════════
// GLOBAL GUI INSTANCE
// ═══════════════════════════════════════════════════════════════════════════════
inline AlphaGUI& getGUI() noexcept {
    static AlphaGUI instance;
    return instance;
}

}  // namespace GUI
}  // namespace Alpha
