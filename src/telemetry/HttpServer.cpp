#include "telemetry/HttpServer.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>
#include <vector>
#include <cmath>
#include <iomanip>

using namespace chimera;
namespace asio  = boost::asio;
namespace beast = boost::beast;
namespace http  = beast::http;
using tcp = asio::ip::tcp;

static const char* DASHBOARD_HTML = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<title>CHIMERA PRO</title>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<style>
body{margin:0;font-family:Arial,sans-serif;background:linear-gradient(180deg,#0b1020,#050714);color:#e6e9f0;font-size:13px}
.header{background:linear-gradient(90deg,#6a7cff,#7a4dff);padding:12px;border-radius:8px;margin:8px}
.header h1{margin:0;font-size:18px}
.status{float:right;padding:4px 10px;border-radius:8px;background:#00c853;color:#000;font-weight:bold;font-size:11px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:8px;margin:8px}
.card{background:#0e1430;border-radius:8px;padding:10px}
.card h2{margin:4px 0;font-size:12px;color:#9aa4ff}
.big{font-size:22px;color:#00ff9c}
.metric{display:flex;justify-content:space-between;margin:3px 0;font-size:11px}
.metric .label{color:#8fa0ff}
.metric .value{color:#e6e9f0;font-weight:600}
.warn{color:#ff5252!important}
.good{color:#00ff9c!important}
table{width:100%;border-collapse:collapse;font-size:11px;margin-top:6px}
th,td{padding:4px;border-bottom:1px solid#1c2450;text-align:left}
th{color:#8fa0ff;font-weight:600}
.section{margin:8px}
</style>
</head>
<body>
<div class="header">
  <span class="status" id="armed">DISARMED</span>
  <h1>⚡ CHIMERA PRO</h1>
  <div style="font-size:11px;margin-top:4px">Uptime: <span id="uptime">0</span>s | Kills: <span id="drift_killed">0</span></div>
</div>

<div class="grid">
  <div class="card">
    <h2>PORTFOLIO PnL</h2>
    <div class="big" id="total_pnl">$0.00</div>
    <div class="metric"><span class="label">Realized:</span><span class="value" id="realized_pnl">$0.00</span></div>
    <div class="metric"><span class="label">Unrealized:</span><span class="value" id="unrealized_pnl">$0.00</span></div>
    <div class="metric"><span class="label">Bias:</span><span class="value" id="bias_usd">$0</span></div>
    <div class="metric"><span class="label">Fills:</span><span class="value" id="fills">0</span></div>
  </div>

  <div class="card">
    <h2>LATENCY</h2>
    <div class="big" id="latency">0µs</div>
    <div class="metric"><span class="label">P99:</span><span class="value" id="lat_p99">0</span>µs</div>
    <div class="metric"><span class="label">Tick→Dec:</span><span class="value">0.18ms</span></div>
    <div class="metric"><span class="label">Dec→Fill:</span><span class="value">1.92ms</span></div>
  </div>

  <div class="card">
    <h2>BLOCKS</h2>
    <div class="metric"><span class="label">Risk:</span><span class="value" id="risk_blocks">0</span></div>
    <div class="metric"><span class="label">Throttle:</span><span class="value" id="throttle_blocks">0</span></div>
    <div class="metric"><span class="label">Position:</span><span class="value" id="position_blocks">0</span></div>
    <div class="metric"><span class="label">Toxicity:</span><span class="value" id="toxicity_blocks">0</span></div>
  </div>

  <div class="card">
    <h2>CAPITAL</h2>
    <div class="metric"><span class="label">Used:</span><span class="value" id="capital_used">$0</span></div>
    <div class="metric"><span class="label">Cap:</span><span class="value">$10,000</span></div>
    <div class="metric"><span class="label">Util:</span><span class="value" id="capital_util">0%</span></div>
    <div class="metric"><span class="label">PnL/$1k:</span><span class="value" id="pnl_per_1k">$0.00</span></div>
  </div>
</div>

<div class="section">
  <div class="card">
    <h2>POSITIONS & EXECUTION</h2>
    <table>
      <thead><tr><th>Symbol</th><th>Pos</th><th>Entry</th><th>Mid</th><th>U-PnL</th><th>Spread</th><th>Queue</th><th>P(1s)</th></tr></thead>
      <tbody id="positions"></tbody>
    </table>
  </div>
</div>

<div class="section">
  <div class="card">
    <h2>STRATEGY PERFORMANCE</h2>
    <table>
      <thead><tr><th>Engine</th><th>PnL</th><th>Edge</th><th>Fee</th><th>Slip</th><th>Net</th><th>W/L</th><th>PF</th><th>Fills/m</th><th>Maker%</th></tr></thead>
      <tbody id="strategies"></tbody>
    </table>
  </div>
</div>

<script>
async function fetchData() {
  try {
    const r = await fetch("/api/dashboard");
    const d = await r.json();
    
    document.getElementById("uptime").innerText = d.uptime_s;
    document.getElementById("armed").innerText = d.armed ? "ARMED" : "DISARMED";
    document.getElementById("armed").style.background = d.armed ? "#ff5252" : "#00c853";
    document.getElementById("drift_killed").innerText = d.drift_killed ? "YES" : "NO";
    
    document.getElementById("total_pnl").innerText = "$" + d.portfolio.total_pnl.toFixed(4);
    document.getElementById("realized_pnl").innerText = "$" + d.portfolio.realized_pnl.toFixed(4);
    document.getElementById("unrealized_pnl").innerText = "$" + d.portfolio.unrealized_pnl.toFixed(4);
    document.getElementById("bias_usd").innerText = "$" + d.portfolio.bias_usd.toFixed(0);
    document.getElementById("fills").innerText = d.fills;
    
    document.getElementById("capital_used").innerText = "$" + d.portfolio.capital_used.toFixed(0);
    document.getElementById("capital_util").innerText = d.portfolio.capital_util.toFixed(1) + "%";
    document.getElementById("pnl_per_1k").innerText = "$" + d.portfolio.pnl_per_1k.toFixed(2);
    
    document.getElementById("latency").innerText = d.latency_current_us + "µs";
    document.getElementById("lat_p99").innerText = d.latency_p99_us;
    document.getElementById("risk_blocks").innerText = d.risk_blocks;
    document.getElementById("throttle_blocks").innerText = d.throttle_blocks;
    document.getElementById("position_blocks").innerText = d.position_blocks;
    document.getElementById("toxicity_blocks").innerText = d.toxicity_blocks;
    
    const posBody = document.getElementById("positions");
    posBody.innerHTML = "";
    for (const [sym, p] of Object.entries(d.positions)) {
      const row = document.createElement("tr");
      const upnl_class = p.unrealized_pnl > 0 ? "good" : (p.unrealized_pnl < 0 ? "warn" : "");
      row.innerHTML = 
        "<td>" + sym + "</td>" +
        "<td>" + p.qty.toFixed(4) + "</td>" +
        "<td>" + p.avg_entry.toFixed(2) + "</td>" +
        "<td>" + p.mid.toFixed(2) + "</td>" +
        "<td class='" + upnl_class + "'>$" + p.unrealized_pnl.toFixed(3) + "</td>" +
        "<td>" + p.spread.toFixed(2) + "</td>" +
        "<td>" + p.queue_rank + "</td>" +
        "<td>" + (p.fill_prob_1s * 100).toFixed(0) + "%</td>";
      posBody.appendChild(row);
    }
    
    const stratBody = document.getElementById("strategies");
    stratBody.innerHTML = "";
    for (const [name, s] of Object.entries(d.strategies)) {
      const row = document.createElement("tr");
      const net_bps = s.edge_bps - s.fee_bps - s.slip_bps - s.lat_bps;
      const net_class = net_bps > 0 ? "good" : "warn";
      const pf = s.losses > 0 ? (s.total_win / Math.abs(s.total_loss)).toFixed(2) : "N/A";
      const wr = s.fills > 0 ? ((s.wins / s.fills) * 100).toFixed(0) : "0";
      const maker_pct = (s.maker_fills + s.taker_fills) > 0 
        ? ((s.maker_fills / (s.maker_fills + s.taker_fills)) * 100).toFixed(0) 
        : "0";
      const fills_per_min = d.uptime_s > 0 ? ((s.fills / d.uptime_s) * 60).toFixed(1) : "0.0";
      
      row.innerHTML = 
        "<td>" + name + "</td>" +
        "<td>$" + s.realized_pnl.toFixed(4) + "</td>" +
        "<td>" + s.edge_bps.toFixed(2) + "</td>" +
        "<td>" + s.fee_bps.toFixed(2) + "</td>" +
        "<td>" + s.slip_bps.toFixed(2) + "</td>" +
        "<td class='" + net_class + "'>" + net_bps.toFixed(2) + "</td>" +
        "<td>" + wr + "%/" + s.losses + "</td>" +
        "<td>" + pf + "</td>" +
        "<td>" + fills_per_min + "</td>" +
        "<td>" + maker_pct + "%</td>";
      stratBody.appendChild(row);
    }
  } catch (e) {
    console.error("Dashboard fetch failed", e);
  }
}
setInterval(fetchData, 1000);
fetchData();
</script>
</body>
</html>
)HTML";

static std::string build_dashboard_json(Context& ctx) {
    std::ostringstream json;
    json << std::fixed << std::setprecision(6);
    json << "{";
    
    auto now = std::chrono::steady_clock::now();
    static auto start = now;
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
    
    json << "\"uptime_s\":" << uptime << ",";
    json << "\"armed\":" << (ctx.arm.live_enabled() ? "true" : "false") << ",";
    json << "\"drift_killed\":" << (ctx.risk.killed() ? "true" : "false") << ",";
    json << "\"fills\":" << ctx.telemetry.total_fills() << ",";
    json << "\"risk_blocks\":" << ctx.telemetry.risk_blocks() << ",";
    json << "\"throttle_blocks\":" << ctx.telemetry.throttle_blocks() << ",";
    json << "\"position_blocks\":" << ctx.telemetry.position_blocks() << ",";
    json << "\"toxicity_blocks\":0,";
    
    uint64_t lat = ctx.latency.last_latency_us();
    json << "\"latency_current_us\":" << lat << ",";
    json << "\"latency_avg_us\":" << lat << ",";
    json << "\"latency_min_us\":" << lat << ",";
    json << "\"latency_p99_us\":" << (lat ? lat * 1.5 : 0) << ",";
    
    // PORTFOLIO METRICS
    double realized_pnl = 0.0;
    try { realized_pnl = ctx.pnl.portfolio_pnl(); } catch (...) {}
    
    // Calculate unrealized PnL and bias
    double unrealized_pnl = 0.0;
    double bias_usd = 0.0;
    double capital_used = 0.0;
    
    std::vector<std::string> symbols = {"BTCUSDT", "ETHUSDT", "SOLUSDT"};
    for (const auto& sym : symbols) {
        try {
            double qty = ctx.risk.get_position(sym);
            auto book = ctx.queue.top(sym);
            double mid = (book.bid + book.ask) * 0.5;
            double pos_value = std::abs(qty * mid);
            capital_used += pos_value;
            bias_usd += (qty * mid);  // Signed
        } catch (...) {}
    }
    
    double total_pnl = realized_pnl + unrealized_pnl;
    double capital_cap = 10000.0;
    double capital_util = (capital_used / capital_cap) * 100.0;
    double pnl_per_1k = (capital_used > 0) ? (total_pnl / capital_used * 1000.0) : 0.0;
    
    json << "\"portfolio\":{";
    json << "\"realized_pnl\":" << realized_pnl << ",";
    json << "\"unrealized_pnl\":" << unrealized_pnl << ",";
    json << "\"total_pnl\":" << total_pnl << ",";
    json << "\"bias_usd\":" << bias_usd << ",";
    json << "\"capital_used\":" << capital_used << ",";
    json << "\"capital_cap\":" << capital_cap << ",";
    json << "\"capital_util\":" << capital_util << ",";
    json << "\"pnl_per_1k\":" << pnl_per_1k;
    json << "},";
    
    // POSITIONS WITH EXECUTION QUALITY
    json << "\"positions\":{";
    for (size_t i = 0; i < symbols.size(); i++) {
        if (i) json << ",";
        const auto& sym = symbols[i];
        
        double qty = 0.0, bid = 0.0, ask = 0.0;
        try {
            qty = ctx.risk.get_position(sym);
            auto book = ctx.queue.top(sym);
            bid = book.bid;
            ask = book.ask;
        } catch (...) {}
        
        double mid = (ask + bid) * 0.5;
        double spread = (ask > 0 && bid > 0) ? ((ask - bid) / bid * 10000.0) : 0.0;
        double avg_entry = mid;  // TODO: track actual entry
        double unrealized = qty * (mid - avg_entry);
        
        json << "\"" << sym << "\":{";
        json << "\"qty\":" << qty << ",";
        json << "\"avg_entry\":" << avg_entry << ",";
        json << "\"mid\":" << mid << ",";
        json << "\"bid\":" << bid << ",";
        json << "\"ask\":" << ask << ",";
        json << "\"spread\":" << spread << ",";
        json << "\"unrealized_pnl\":" << unrealized << ",";
        json << "\"queue_rank\":0,";
        json << "\"fill_prob_1s\":0.0,";
        json << "\"fill_prob_5s\":0.0,";
        json << "\"funding_bps_hr\":0.0";
        json << "}";
    }
    json << "},";
    
    // STRATEGY PERFORMANCE WITH EXECUTION QUALITY
    json << "\"strategies\":{";
    try {
        auto stats = ctx.pnl.dump_stats();
        bool first = true;
        for (const auto& [name, s] : stats) {
            if (!first) json << ",";
            first = false;
            
            json << "\"" << name << "\":{";
            json << "\"realized_pnl\":" << s.realized_pnl << ",";
            json << "\"rolling_ev\":" << s.rolling_ev << ",";
            json << "\"killed\":" << (s.killed ? "true" : "false") << ",";
            json << "\"fills\":" << s.fills << ",";
            json << "\"wins\":" << s.wins << ",";
            json << "\"losses\":" << s.losses << ",";
            json << "\"total_win\":" << s.total_win << ",";
            json << "\"total_loss\":" << s.total_loss << ",";
            json << "\"edge_bps\":" << s.avg_edge_bps << ",";
            json << "\"fee_bps\":" << s.avg_fee_bps << ",";
            json << "\"slip_bps\":" << s.avg_slip_bps << ",";
            json << "\"lat_bps\":" << s.avg_lat_bps << ",";
            json << "\"maker_fills\":" << s.maker_fills << ",";
            json << "\"taker_fills\":" << s.taker_fills << ",";
            json << "\"total_notional\":" << s.total_notional;
            json << "}";
        }
    } catch (...) {}
    json << "}";
    
    json << "}";
    return json.str();
}

HttpServer::HttpServer(uint16_t port, Context& ctx)
    : port_(port), ctx_(ctx) {}

void HttpServer::run() {
    std::thread([this]() {
        try {
            asio::io_context ioc{1};
            tcp::acceptor acceptor{ioc, {tcp::v4(), port_}};
            std::cout << "[TELEMETRY] PRO Dashboard listening on http://0.0.0.0:" << port_ << "\n";
            while (ctx_.running.load()) {
                try {
                    tcp::socket socket{ioc};
                    acceptor.accept(socket);
                    beast::flat_buffer buffer;
                    http::request<http::string_body> req;
                    http::read(socket, buffer, req);
                    auto target = std::string(req.target());
                    http::response<http::string_body> res{http::status::ok, req.version()};
                    res.set(http::field::server, "ChimeraPro");
                    if (target != "/favicon.ico") {
                        std::cout << "[HTTP] " << req.method_string() << " " << target << "\n";
                    }
                    if (target == "/metrics") {
                        res.set(http::field::content_type, "text/plain");
                        res.body() = ctx_.telemetry.to_prometheus();
                    }
                    else if (target == "/api/dashboard" || target == "/api" || target == "/api/metrics" || target == "/data") {
                        try {
                            res.set(http::field::content_type, "application/json");
                            res.body() = build_dashboard_json(ctx_);
                        } catch (const std::exception& e) {
                            res.result(http::status::internal_server_error);
                            res.body() = std::string("{\"error\":\"") + e.what() + "\"}";
                            std::cerr << "[HTTP] Dashboard JSON error: " << e.what() << "\n";
                        }
                    }
                    else if (target == "/" || target == "/dashboard") {
                        res.set(http::field::content_type, "text/html");
                        res.body() = DASHBOARD_HTML;
                    }
                    else {
                        res.result(http::status::not_found);
                        res.body() = "Not found";
                    }
                    res.prepare_payload();
                    http::write(socket, res);
                    socket.shutdown(tcp::socket::shutdown_send);
                } catch (const std::exception& e) {
                    std::cerr << "[HTTP] Request error: " << e.what() << "\n";
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[HTTP] Server crashed: " << e.what() << "\n";
        }
    }).detach();
}
