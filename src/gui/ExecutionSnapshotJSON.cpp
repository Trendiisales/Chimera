#include "gui/ExecutionSnapshot.hpp"
#include <sstream>
#include <iomanip>

namespace gui {

static std::string q(const std::string& s) {
    return "\"" + s + "\"";
}

static std::string formatUptime(uint64_t seconds) {
    uint64_t hours = seconds / 3600;
    uint64_t mins = (seconds % 3600) / 60;
    uint64_t secs = seconds % 60;
    
    std::ostringstream o;
    o << std::setfill('0') << std::setw(2) << hours << "h:"
      << std::setw(2) << mins << "m:"
      << std::setw(2) << secs << "s";
    return o.str();
}

std::string EmitJSON(const ExecutionSnapshot& s) {
    std::ostringstream o;
    o << "{";
    o << "\"ts\":" << s.ts << ",";
    
    // Calculate aggregated exec stats
    int total_trades = 0;
    int total_rejects = 0;
    double total_pnl = 0.0;
    
    // Emit xau/xag directly at root
    for (auto& [name, sym] : s.symbols) {
        std::string key = (name == "XAUUSD") ? "xau" :
                          (name == "XAGUSD") ? "xag" : name;
        
        total_trades += sym.trades;
        total_rejects += sym.rejects;
        total_pnl += sym.pnl.shadow;
        
        o << q(key) << ":{";
        o << "\"bid\":" << sym.bid << ",";
        o << "\"ask\":" << sym.ask << ",";
        o << "\"spread\":" << sym.spread << ",";
        o << "\"latency_ms\":" << sym.latency_ms << ",";
        o << "\"trades\":" << sym.trades << ",";
        o << "\"rejects\":" << sym.rejects << ",";
        o << "\"legs\":" << sym.legs << ",";
        o << "\"session\":" << q(sym.session) << ",";
        o << "\"regime\":" << q(sym.regime) << ",";
        o << "\"state\":" << q(sym.state) << ",";
        o << "\"gates\":{";
        bool fg = true;
        for (auto& [g, st] : sym.gates) {
            if (!fg) o << ",";
            fg = false;
            o << q(g) << ":{\"ok\":" << (st.ok?"true":"false")
              << ",\"reason\":" << q(st.reason) << "}";
        }
        o << "},";
        o << "\"cost\":{\"total\":" << sym.cost.total_bps << "},";
        o << "\"edge\":{\"raw\":" << sym.edge.raw_bps
          << ",\"latency_adj\":" << sym.edge.latency_adj_bps
          << ",\"required\":" << sym.edge.required_bps << "},";
        o << "\"impulse\":{\"raw\":" << sym.impulse.raw
          << ",\"latency_adj\":" << sym.impulse.latency_adj
          << ",\"min\":" << sym.impulse.min_required << "},";
        o << "\"pnl\":{\"shadow\":" << sym.pnl.shadow
          << ",\"cash\":" << sym.pnl.cash << "}";
        o << "},";
    }
    
    // Add exec aggregation
    o << "\"exec\":{";
    o << "\"pnl\":" << total_pnl << ",";
    o << "\"trades\":" << total_trades << ",";
    o << "\"rejects\":" << total_rejects;
    o << "},";
    
    // Add blotter
    o << "\"blotter\":[";
    bool first_trade = true;
    for (const auto& trade : s.blotter) {
        if (!first_trade) o << ",";
        first_trade = false;
        o << "{";
        o << "\"id\":" << trade.id << ",";
        o << "\"sym\":" << q(trade.sym) << ",";
        o << "\"side\":" << q(std::string(1, trade.side)) << ",";
        o << "\"qty\":" << trade.qty << ",";
        o << "\"entry\":" << trade.entry << ",";
        o << "\"exit\":" << trade.exit << ",";
        o << "\"fees\":" << trade.fees << ",";
        o << "\"pnl\":" << trade.pnl;
        o << "}";
    }
    o << "],";
    
    // Add latency with regime
    o << "\"latency\":{";
    o << "\"fix_rtt_ms\":" << (s.symbols.empty() ? 0.0 : s.symbols.begin()->second.latency_ms) << ",";
    o << "\"regime\":" << q(s.symbols.empty() ? "UNKNOWN" : s.symbols.begin()->second.regime);
    o << "},";
    
    // Add meta with formatted uptime
    o << "\"meta\":{";
    o << "\"uptime\":" << q(formatUptime(s.ts)) << ",";
    o << "\"shadow\":true";
    o << "},";
    
    o << "\"governor\":{"
      << "\"daily_dd\":" << q(s.governor.daily_dd) << ","
      << "\"hourly_loss\":" << q(s.governor.hourly_loss) << ","
      << "\"reject_rate\":" << q(s.governor.reject_rate) << ","
      << "\"action\":" << q(s.governor.action)
      << "},";
    
    o << "\"connections\":{"
      << "\"fix\":" << (s.connections.fix?"true":"false") << ","
      << "\"ctrader\":" << (s.connections.ctrader?"true":"false")
      << "}";
    
    o << "}";
    return o.str();
}

} // namespace gui
