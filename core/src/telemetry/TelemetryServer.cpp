#include "chimera/telemetry/TelemetryServer.hpp"
#include "chimera/telemetry_bridge/GuiState.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sstream>
#include <cstring>

namespace chimera {

TelemetryServer::TelemetryServer(int port) : port_(port) {}
TelemetryServer::~TelemetryServer() { stop(); }

void TelemetryServer::start() {
    if (running_) return;
    running_ = true;
    thread_ = std::thread(&TelemetryServer::run, this);
}

void TelemetryServer::stop() {
    if (!running_) return;
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

static void send_all(int fd, const std::string& s) {
    const char* p = s.c_str();
    size_t n = s.size();
    while (n) {
        ssize_t w = ::send(fd, p, n, 0);
        if (w <= 0) return;
        p += w;
        n -= (size_t)w;
    }
}

void TelemetryServer::handle_client(int fd) {
    char buf[2048];
    int r = ::recv(fd, buf, sizeof(buf)-1, 0);
    if (r <= 0) { ::close(fd); return; }
    buf[r] = 0;
    std::string req(buf);

    if (req.find("GET /json") == 0) {
        std::string body = build_json();
        std::ostringstream os;
        os << "HTTP/1.1 200 OK\r\n"
           << "Content-Type: application/json\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Connection: close\r\n\r\n"
           << body;
        send_all(fd, os.str());
    } else {
        extern const char* g_dashboard_html;
        std::string body(g_dashboard_html);
        std::ostringstream os;
        os << "HTTP/1.1 200 OK\r\n"
           << "Content-Type: text/html\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Connection: close\r\n\r\n"
           << body;
        send_all(fd, os.str());
    }
    ::close(fd);
}

void TelemetryServer::run() {
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port_);

    if (::bind(s, (sockaddr*)&addr, sizeof(addr)) != 0) return;
    if (::listen(s, 16) != 0) return;

    while (running_) {
        int c = ::accept(s, nullptr, nullptr);
        if (c >= 0) handle_client(c);
    }
    ::close(s);
}

std::string TelemetryServer::build_json() {
    auto& gs = GuiState::instance();
    std::lock_guard<std::mutex> lk(gs.mtx);
    std::ostringstream os;
    
    os << "{";
    
    // System state
    os << "\"system\":{"
       << "\"mode\":\"" << gs.system.mode << "\","
       << "\"governor_mode\":\"" << gs.system.governor_mode << "\","
       << "\"build_id\":\"" << gs.system.build_id << "\","
       << "\"uptime_s\":" << gs.system.uptime_s << ","
       << "\"clock_drift_ms\":" << gs.system.clock_drift_ms << ","
       << "\"kill_switch\":" << (gs.system.kill_switch ? "true" : "false")
       << "},";
    
    // Latency state
    os << "\"latency\":{"
       << "\"tick_to_decision_ms\":" << gs.latency.tick_to_decision_ms << ","
       << "\"decision_to_send_ms\":" << gs.latency.decision_to_send_ms << ","
       << "\"send_to_ack_ms\":" << gs.latency.send_to_ack_ms << ","
       << "\"ack_to_fill_ms\":" << gs.latency.ack_to_fill_ms << ","
       << "\"rtt_total_ms\":" << gs.latency.rtt_total_ms << ","
       << "\"slippage_bps\":" << gs.latency.slippage_bps << ","
       << "\"venue\":\"" << gs.latency.venue << "\""
       << "},";
    
    // PnL state
    os << "\"pnl\":{"
       << "\"realized_bps\":" << gs.pnl.realized_bps << ","
       << "\"unrealized_bps\":" << gs.pnl.unrealized_bps << ","
       << "\"daily_dd_bps\":" << gs.pnl.daily_dd_bps << ","
       << "\"risk_limit_bps\":" << gs.pnl.risk_limit_bps
       << "},";
    
    // Governor state
    os << "\"governor\":{"
       << "\"recommendation\":\"" << gs.governor.recommendation << "\","
       << "\"confidence\":" << gs.governor.confidence << ","
       << "\"survival_bps\":" << gs.governor.survival_bps << ","
       << "\"cooldown_s\":" << gs.governor.cooldown_s << ","
       << "\"last_action\":\"" << gs.governor.last_action << "\""
       << "},";
    
    // Symbols array
    os << "\"symbols\":[";
    for (size_t i = 0; i < gs.symbols.size(); ++i) {
        const auto& sym = gs.symbols[i];
        os << "{"
           << "\"symbol\":\"" << sym.symbol << "\","
           << "\"hash\":" << sym.hash << ","
           << "\"bid\":" << sym.bid << ","
           << "\"ask\":" << sym.ask << ","
           << "\"last\":" << sym.last << ","
           << "\"spread_bps\":" << sym.spread_bps << ","
           << "\"ofi\":" << sym.ofi << ","
           << "\"regime\":\"" << sym.regime << "\","
           << "\"volatility\":" << sym.volatility << ","
           << "\"correlation\":" << sym.correlation << ","
           << "\"depth\":" << sym.depth << ","
           << "\"engine\":\"" << sym.engine << "\","
           << "\"capital_weight\":" << sym.capital_weight << ","
           << "\"enabled\":" << (sym.enabled ? "true" : "false")
           << "}";
        if (i + 1 < gs.symbols.size()) os << ",";
    }
    os << "],";
    
    // Trades array
    os << "\"trades\":[";
    for (size_t i = 0; i < gs.trades.size(); ++i) {
        const auto& trade = gs.trades[i];
        os << "{"
           << "\"id\":" << trade.id << ","
           << "\"time\":\"" << trade.time << "\","
           << "\"symbol\":\"" << trade.symbol << "\","
           << "\"engine\":\"" << trade.engine << "\","
           << "\"side\":\"" << trade.side << "\","
           << "\"qty\":" << trade.qty << ","
           << "\"entry\":" << trade.entry << ","
           << "\"exit\":" << trade.exit << ","
           << "\"pnl_bps\":" << trade.pnl_bps << ","
           << "\"slippage_bps\":" << trade.slippage_bps << ","
           << "\"latency_ms\":" << trade.latency_ms << ","
           << "\"regime\":\"" << trade.regime << "\","
           << "\"signals\":{"
           << "\"ofi\":" << trade.signals.ofi << ","
           << "\"impulse\":" << trade.signals.impulse << ","
           << "\"funding\":" << trade.signals.funding << ","
           << "\"volatility\":" << trade.signals.volatility << ","
           << "\"correlation\":" << trade.signals.correlation << ","
           << "\"levels\":" << trade.signals.levels
           << "}}";
        if (i + 1 < gs.trades.size()) os << ",";
    }
    os << "]";
    
    os << "}";
    return os.str();
}

} // namespace chimera
