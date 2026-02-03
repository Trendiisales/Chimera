#include "telemetry/HttpServer.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <vector>
#include <iomanip>

using namespace chimera;
namespace asio  = boost::asio;
namespace beast = boost::beast;
namespace http  = beast::http;
using tcp = asio::ip::tcp;

HttpServer::HttpServer(uint16_t port, Context& ctx)
    : port_(port), ctx_(ctx) {}

static std::string build_dashboard_json(Context& ctx) {
    try {
        std::ostringstream json;
        json << std::fixed << std::setprecision(6);
        
        json << "{";
        
        // System metrics
        auto now = std::chrono::steady_clock::now();
        static auto start_time = now;
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
        
        json << "\"uptime_s\":" << uptime << ",";
        
        try {
            json << "\"armed\":" << (ctx.arm.live_enabled() ? "true" : "false") << ",";
        } catch (...) {
            json << "\"armed\":false,";
        }
        
        try {
            json << "\"drift_killed\":" << (ctx.risk.killed() ? "true" : "false") << ",";
        } catch (...) {
            json << "\"drift_killed\":false,";
        }
        
        // Counts
        json << "\"fills\":" << ctx.telemetry.total_fills() << ",";
        json << "\"risk_blocks\":" << ctx.telemetry.risk_blocks() << ",";
        json << "\"throttle_blocks\":" << ctx.telemetry.throttle_blocks() << ",";
        json << "\"position_blocks\":" << ctx.telemetry.position_blocks() << ",";
        json << "\"toxicity_blocks\":0,";
        
        // Latency metrics
        json << "\"latency_current_us\":0,";
        json << "\"latency_avg_us\":0,";
        json << "\"latency_min_us\":0,";
        json << "\"latency_p99_us\":0,";
        
        // PnL
        double total_pnl = 0.0;
        try {
            total_pnl = ctx.pnl.portfolio_pnl();
        } catch (...) {}
        
        json << "\"pnl\":" << total_pnl << ",";
        json << "\"pnl_unrealized\":0,";
        json << "\"pnl_realized\":" << total_pnl << ",";
        json << "\"fees_paid\":0,";
        
        // Positions
        json << "\"positions\":{";
        try {
            std::vector<std::string> symbols = {"BTCUSDT", "ETHUSDT", "SOLUSDT"};
            bool first_pos = true;
            for (const auto& sym : symbols) {
                try {
                    double pos = ctx.risk.get_position(sym);
                    auto book = ctx.queue.top(sym);
                    
                    if (!first_pos) json << ",";
                    first_pos = false;
                    
                    json << "\"" << sym << "\":{";
                    json << "\"qty\":" << pos << ",";
                    json << "\"bid\":" << book.bid << ",";
                    json << "\"ask\":" << book.ask;
                    json << "}";
                } catch (...) {
                    // Skip this symbol
                }
            }
        } catch (...) {}
        json << "},";
        
        // Strategies
        json << "\"strategies\":{";
        try {
            auto all_stats = ctx.pnl.dump_stats();
            bool first_strat = true;
            for (const auto& [strat_name, stats] : all_stats) {
                if (!first_strat) json << ",";
                first_strat = false;
                
                json << "\"" << strat_name << "\":{";
                json << "\"pnl\":" << stats.realized_pnl << ",";
                json << "\"fills\":0,";
                json << "\"win_pct\":0";
                json << "}";
            }
        } catch (...) {}
        json << "},";
        
        // Fills tape
        json << "\"fills_tape\":[],";
        
        // Regime
        json << "\"regime\":{";
        json << "\"name\":\"NORMAL\",";
        json << "\"vol_bps\":0,";
        json << "\"trend\":0,";
        json << "\"signal\":\"NEUTRAL\"";
        json << "},";
        
        // System health
        json << "\"cpu_pct\":0,";
        json << "\"mem_mb\":0";
        
        json << "}";
        return json.str();
        
    } catch (const std::exception& e) {
        // Return minimal valid JSON on error
        return "{\"uptime_s\":0,\"armed\":false,\"fills\":0,\"pnl\":0.0,\"positions\":{},\"strategies\":{},\"error\":\"" + std::string(e.what()) + "\"}";
    } catch (...) {
        return "{\"uptime_s\":0,\"armed\":false,\"fills\":0,\"pnl\":0.0,\"positions\":{},\"strategies\":{},\"error\":\"unknown\"}";
    }
}

static std::string read_file(const std::string& path) {
    std::vector<std::string> search_paths = {
        path,
        "../" + path,
        "../../" + path,
        "../../../" + path,
        "src/telemetry/" + path,
        "../src/telemetry/" + path,
        "../../src/telemetry/" + path
    };
    
    for (const auto& full_path : search_paths) {
        std::ifstream file(full_path);
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::cout << "[TELEMETRY] Loaded file from: " << full_path << "\n";
            return buffer.str();
        }
    }
    
    return "";
}

void HttpServer::run() {
    try {
        asio::io_context ioc;
        tcp::acceptor acceptor(ioc, {tcp::v4(), port_});
        acceptor.set_option(tcp::acceptor::reuse_address(true));
        acceptor.non_blocking(true);

        std::cout << "[TELEMETRY] HTTP Server listening on http://0.0.0.0:" << port_ << "\n";
        std::cout << "[TELEMETRY] Dashboard: http://localhost:" << port_ << "/\n";
        std::cout << "[TELEMETRY] API Data:  http://localhost:" << port_ << "/api/dashboard\n";
        std::cout << "[TELEMETRY] Metrics:   http://localhost:" << port_ << "/metrics\n";

        while (ctx_.running.load()) {
            tcp::socket socket(ioc);
            beast::error_code ec;
            acceptor.accept(socket, ec);

            if (ec == asio::error::would_block) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            if (ec) continue;

            try {
                beast::flat_buffer buffer;
                http::request<http::string_body> req;
                http::read(socket, buffer, req);

                http::response<http::string_body> res;
                res.version(req.version());
                res.result(http::status::ok);
                res.set(http::field::server, "chimera-v22");
                res.set(http::field::access_control_allow_origin, "*");

                std::string target = std::string(req.target());
                
                if (target == "/metrics") {
                    res.set(http::field::content_type, "text/plain");
                    res.body() = ctx_.telemetry.to_prometheus();
                    
                } else if (target == "/api/dashboard" || target == "/api" || target == "/data") {
                    try {
                        res.set(http::field::content_type, "application/json");
                        res.body() = build_dashboard_json(ctx_);
                    } catch (const std::exception& e) {
                        res.result(http::status::internal_server_error);
                        res.set(http::field::content_type, "text/plain");
                        res.body() = std::string("Error building dashboard JSON: ") + e.what();
                    }
                    
                } else if (target == "/" || target == "/dashboard" || target == "/dashboard/") {
                    std::string html = read_file("dashboard.html");
                    if (html.empty()) {
                        res.result(http::status::not_found);
                        res.set(http::field::content_type, "text/plain");
                        res.body() = "Dashboard HTML not found. Tried:\n"
                                   "  - src/telemetry/dashboard.html\n"
                                   "  - dashboard.html\n"
                                   "Ensure the file exists in the source tree.";
                    } else {
                        res.set(http::field::content_type, "text/html");
                        res.body() = html;
                    }
                    
                } else {
                    res.result(http::status::not_found);
                    res.set(http::field::content_type, "text/plain");
                    res.body() = "Not found\n\nAvailable endpoints:\n"
                                "  /              - Dashboard UI\n"
                                "  /api/dashboard - JSON data\n"
                                "  /metrics       - Prometheus metrics\n";
                }

                res.prepare_payload();
                http::write(socket, res);
            } catch (...) {
                // Malformed request — drop
            }
        }
    } catch (const std::exception& e) {
        std::cout << "[TELEMETRY] HTTP Server error: " << e.what() << "\n";
    }
}
