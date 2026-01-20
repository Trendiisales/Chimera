#pragma once

#include <string>
#include <sstream>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include "TradeLogger.hpp"

class MetricsHTTPServer {
public:
    struct LaneMetrics {
        std::string symbol;
        double bid = 0;
        double ask = 0;
        
        // THE 4 CRITICAL LATENCIES FOR HFT
        int64_t l1_market_data_us = 0;   // Exchange event → VPS receive (network + processing)
        int64_t l2_decision_us = 0;       // Tick receive → Signal generated (strategy compute)
        int64_t l3_order_transit_us = 0;  // Order sent → Exchange ack (network round-trip)
        int64_t l4_fill_confirm_us = 0;   // Fill event → Fill received (execution reporting)
        int64_t total_latency_us = 0;     // L1+L2+L3 (critical path)
        
        uint64_t messages = 0;
        uint64_t signals = 0;
        uint64_t trades = 0;
        std::string regime = "UNKNOWN";
        std::vector<TradeLogger::Event> recent_events;
        
        // Depth integrity monitoring
        uint64_t sequence_gaps = 0;
        bool depth_synced = false;
    };
    
    MetricsHTTPServer(int port) : port_(port), running_(false), server_fd_(-1) {}
    
    ~MetricsHTTPServer() { stop(); }
    
    void start() {
        running_ = true;
        thread_ = std::thread(&MetricsHTTPServer::run, this);
    }
    
    void stop() {
        running_ = false;
        if (server_fd_ >= 0) {
            close(server_fd_);
            server_fd_ = -1;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    
    void updateMetrics(const LaneMetrics& metrics) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        bool found = false;
        for (auto& m : metrics_) {
            if (m.symbol == metrics.symbol) {
                m = metrics;
                found = true;
                break;
            }
        }
        
        if (!found) {
            metrics_.push_back(metrics);
        }
    }

private:
    void run() {
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) return;
        
        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);
        
        if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) return;
        if (listen(server_fd_, 3) < 0) return;
        
        while (running_) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
            
            if (client_fd < 0) continue;
            
            // Read HTTP request to determine path
            char buffer[4096] = {0};
            read(client_fd, buffer, sizeof(buffer) - 1);
            std::string request(buffer);
            
            // Route based on path
            if (request.find("GET /metrics") != std::string::npos) {
                // Serve JSON metrics
                std::string json = buildJSON();
                std::string response = 
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Content-Length: " + std::to_string(json.length()) + "\r\n"
                    "\r\n" + json;
                
                send(client_fd, response.c_str(), response.length(), 0);
            } else {
                // Serve HTML dashboard
                std::string html = buildDashboard();
                std::string response = 
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html\r\n"
                    "Content-Length: " + std::to_string(html.length()) + "\r\n"
                    "\r\n" + html;
                
                send(client_fd, response.c_str(), response.length(), 0);
            }
            
            close(client_fd);
        }
    }
    
    std::string buildJSON() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::ostringstream ss;
        ss << "{\"lanes\":[";
        
        for (size_t i = 0; i < metrics_.size(); i++) {
            const auto& m = metrics_[i];
            
            if (i > 0) ss << ",";
            
            ss << "{"
               << "\"symbol\":\"" << m.symbol << "\","
               << "\"bid\":" << m.bid << ","
               << "\"ask\":" << m.ask << ","
               << "\"l1_market_data_us\":" << m.l1_market_data_us << ","
               << "\"l2_decision_us\":" << m.l2_decision_us << ","
               << "\"l3_order_transit_us\":" << m.l3_order_transit_us << ","
               << "\"l4_fill_confirm_us\":" << m.l4_fill_confirm_us << ","
               << "\"total_latency_us\":" << m.total_latency_us << ","
               << "\"messages\":" << m.messages << ","
               << "\"signals\":" << m.signals << ","
               << "\"trades\":" << m.trades << ","
               << "\"regime\":\"" << m.regime << "\","
               << "\"events\":[";
            
            for (size_t j = 0; j < m.recent_events.size(); j++) {
                const auto& e = m.recent_events[j];
                
                if (j > 0) ss << ",";
                
                ss << "{"
                   << "\"timestamp\":" << e.timestamp_us << ","
                   << "\"type\":\"" << e.event_type << "\","
                   << "\"engine\":\"" << e.engine << "\","
                   << "\"regime\":\"" << e.regime << "\","
                   << "\"gate\":\"" << e.gate_reason << "\","
                   << "\"price\":" << e.price << ","
                   << "\"size\":" << e.size << ","
                   << "\"side\":\"" << e.side << "\","
                   << "\"details\":\"" << e.details << "\""
                   << "}";
            }
            
            ss << "]}";
        }
        
        ss << "]}";
        return ss.str();
    }
    
    std::string buildDashboard() {
        return R"(<!DOCTYPE html>
<html>
<head>
    <title>Chimera v4.7 Dashboard</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { 
            font-family: 'SF Mono', 'Monaco', 'Consolas', monospace; 
            background: #0a0e1a; 
            color: #c9d1d9; 
            padding: 20px;
        }
        .header { 
            border-bottom: 2px solid #30363d; 
            padding-bottom: 15px; 
            margin-bottom: 20px;
        }
        h1 { 
            color: #58a6ff; 
            font-size: 24px; 
            font-weight: 600;
        }
        .timestamp { 
            color: #8b949e; 
            font-size: 12px; 
            margin-top: 5px;
        }
        .lanes { 
            display: grid; 
            grid-template-columns: 1fr 1fr; 
            gap: 20px;
        }
        .lane { 
            background: #161b22; 
            border: 1px solid #30363d; 
            border-radius: 6px; 
            padding: 16px;
        }
        .lane-header { 
            display: flex; 
            justify-content: space-between; 
            align-items: center; 
            margin-bottom: 16px; 
            padding-bottom: 12px; 
            border-bottom: 1px solid #21262d;
        }
        .symbol { 
            font-size: 18px; 
            font-weight: 700; 
            color: #58a6ff;
        }
        .regime { 
            padding: 4px 8px; 
            border-radius: 4px; 
            font-size: 11px; 
            font-weight: 600;
        }
        .regime.NORMAL { background: #1f6feb33; color: #58a6ff; }
        .regime.FORCED_FLOW { background: #56d36433; color: #56d364; }
        .regime.NEWS { background: #f8514933; color: #f85149; }
        .regime.DEAD { background: #8b949e33; color: #8b949e; }
        
        .metrics-grid { 
            display: grid; 
            grid-template-columns: repeat(3, 1fr); 
            gap: 8px; 
            margin-bottom: 16px;
        }
        .metric { 
            background: #0d1117; 
            padding: 10px; 
            border-radius: 4px; 
            border-left: 3px solid #30363d;
        }
        .metric-label { 
            color: #8b949e; 
            font-size: 10px; 
            text-transform: uppercase; 
            letter-spacing: 0.5px; 
            margin-bottom: 4px;
        }
        .metric-value { 
            color: #c9d1d9; 
            font-size: 16px; 
            font-weight: 600;
        }
        .metric.price { border-left-color: #58a6ff; }
        .metric.signals { border-left-color: #56d364; }
        .metric.trades { border-left-color: #f0883e; }
        
        .latency-section { 
            background: #0d1117; 
            padding: 12px; 
            border-radius: 4px; 
            margin-bottom: 16px;
        }
        .latency-title { 
            color: #8b949e; 
            font-size: 11px; 
            font-weight: 600; 
            margin-bottom: 8px; 
            text-transform: uppercase;
        }
        .latency-bars { 
            display: grid; 
            gap: 6px;
        }
        .latency-bar { 
            display: flex; 
            align-items: center; 
            gap: 8px;
        }
        .latency-label { 
            min-width: 120px; 
            font-size: 11px; 
            color: #8b949e;
        }
        .latency-fill { 
            height: 20px; 
            border-radius: 3px; 
            display: flex; 
            align-items: center; 
            padding: 0 8px; 
            font-size: 11px; 
            font-weight: 600;
        }
        .latency-fill.good { background: #238636; color: #fff; }
        .latency-fill.warn { background: #9e6a03; color: #fff; }
        .latency-fill.bad { background: #da3633; color: #fff; }
        
        .events-section { 
            background: #0d1117; 
            border-radius: 4px; 
            padding: 12px;
        }
        .events-title { 
            color: #8b949e; 
            font-size: 11px; 
            font-weight: 600; 
            margin-bottom: 8px; 
            text-transform: uppercase;
        }
        .event { 
            background: #161b22; 
            border-left: 3px solid #30363d; 
            padding: 8px; 
            margin-bottom: 6px; 
            border-radius: 3px;
        }
        .event.SIGNAL { border-left-color: #56d364; }
        .event.GATE { border-left-color: #f0883e; }
        .event.EXEC { border-left-color: #58a6ff; }
        .event.FILL { border-left-color: #a371f7; }
        .event-header { 
            display: flex; 
            justify-content: space-between; 
            margin-bottom: 4px;
        }
        .event-type { 
            font-size: 10px; 
            font-weight: 600; 
            text-transform: uppercase;
        }
        .event-type.SIGNAL { color: #56d364; }
        .event-type.GATE { color: #f0883e; }
        .event-type.EXEC { color: #58a6ff; }
        .event-type.FILL { color: #a371f7; }
        .event-time { 
            font-size: 10px; 
            color: #6e7681;
        }
        .event-details { 
            font-size: 11px; 
            color: #8b949e; 
            line-height: 1.4;
        }
        .event-details strong { 
            color: #c9d1d9; 
            font-weight: 600;
        }
        .no-events { 
            color: #6e7681; 
            font-size: 11px; 
            font-style: italic; 
            text-align: center; 
            padding: 20px;
        }
    </style>
</head>
<body>
    <div class="header">
        <h1>CHIMERA v4.7 LIVE</h1>
        <div class="timestamp" id="timestamp"></div>
    </div>
    <div class="lanes" id="lanes"></div>
    <script>
        function formatTime(ts) {
            let d = new Date(ts / 1000);
            return d.toLocaleTimeString();
        }
        
        function update() {
            document.getElementById('timestamp').textContent = new Date().toLocaleString();
            
            fetch('/metrics')
                .then(r => r.json())
                .then(data => {
                    let html = '';
                    
                    data.lanes.forEach(lane => {
                        let l1 = lane.l1_market_data_us / 1000;
                        let l2 = lane.l2_decision_us / 1000;
                        let l3 = lane.l3_order_transit_us / 1000;
                        let l4 = lane.l4_fill_confirm_us / 1000;
                        let total = lane.total_latency_us / 1000;
                        
                        let latencyClass = total < 50 ? 'good' : total < 80 ? 'warn' : 'bad';
                        let latencyStatus = total < 50 ? 'EXCELLENT' : total < 80 ? 'VIABLE' : 'TOO SLOW';
                        
                        html += '<div class="lane">';
                        
                        // Header
                        html += '<div class="lane-header">';
                        html += '<div class="symbol">' + lane.symbol + '</div>';
                        html += '<div class="regime ' + lane.regime + '">' + lane.regime + '</div>';
                        html += '</div>';
                        
                        // Metrics Grid
                        html += '<div class="metrics-grid">';
                        html += '<div class="metric price">';
                        html += '<div class="metric-label">Price</div>';
                        html += '<div class="metric-value">$' + lane.bid.toFixed(2) + '</div>';
                        html += '</div>';
                        
                        html += '<div class="metric">';
                        html += '<div class="metric-label">Messages</div>';
                        html += '<div class="metric-value">' + lane.messages.toLocaleString() + '</div>';
                        html += '</div>';
                        
                        html += '<div class="metric signals">';
                        html += '<div class="metric-label">Signals</div>';
                        html += '<div class="metric-value">' + lane.signals + '</div>';
                        html += '</div>';
                        
                        html += '<div class="metric trades">';
                        html += '<div class="metric-label">Trades</div>';
                        html += '<div class="metric-value">' + lane.trades + '</div>';
                        html += '</div>';
                        
                        html += '<div class="metric">';
                        html += '<div class="metric-label">Spread</div>';
                        html += '<div class="metric-value">' + ((lane.ask - lane.bid) / lane.bid * 10000).toFixed(2) + ' bps</div>';
                        html += '</div>';
                        
                        html += '<div class="metric">';
                        html += '<div class="metric-label">Status</div>';
                        html += '<div class="metric-value">' + latencyStatus + '</div>';
                        html += '</div>';
                        
                        html += '</div>';
                        
                        // Latency Section
                        html += '<div class="latency-section">';
                        html += '<div class="latency-title">Latency Profile</div>';
                        html += '<div class="latency-bars">';
                        
                        html += '<div class="latency-bar">';
                        html += '<div class="latency-label">L1 Market Data</div>';
                        html += '<div class="latency-fill ' + (l1 < 20 ? 'good' : l1 < 40 ? 'warn' : 'bad') + '" style="width:' + Math.min(l1 * 2, 100) + '%">' + l1.toFixed(2) + ' ms</div>';
                        html += '</div>';
                        
                        html += '<div class="latency-bar">';
                        html += '<div class="latency-label">L2 Decision</div>';
                        html += '<div class="latency-fill ' + (l2 < 1 ? 'good' : l2 < 5 ? 'warn' : 'bad') + '" style="width:' + Math.min(l2 * 20, 100) + '%">' + l2.toFixed(2) + ' ms</div>';
                        html += '</div>';
                        
                        html += '<div class="latency-bar">';
                        html += '<div class="latency-label">L3 Order Transit</div>';
                        html += '<div class="latency-fill ' + (l3 < 30 ? 'good' : l3 < 50 ? 'warn' : 'bad') + '" style="width:' + (l3 > 0 ? Math.min(l3 * 2, 100) : 5) + '%">' + (l3 > 0 ? l3.toFixed(2) + ' ms' : 'N/A') + '</div>';
                        html += '</div>';
                        
                        html += '<div class="latency-bar">';
                        html += '<div class="latency-label">TOTAL (L1+L2+L3)</div>';
                        html += '<div class="latency-fill ' + latencyClass + '" style="width:' + Math.min(total * 2, 100) + '%">' + total.toFixed(2) + ' ms</div>';
                        html += '</div>';
                        
                        html += '</div></div>';
                        
                        // Events Section
                        html += '<div class="events-section">';
                        html += '<div class="events-title">Recent Events (' + (lane.events ? lane.events.length : 0) + ')</div>';
                        
                        if (lane.events && lane.events.length > 0) {
                            lane.events.slice().reverse().slice(0, 8).forEach(evt => {
                                html += '<div class="event ' + evt.type + '">';
                                html += '<div class="event-header">';
                                html += '<span class="event-type ' + evt.type + '">' + evt.type + '</span>';
                                html += '<span class="event-time">' + formatTime(evt.timestamp) + '</span>';
                                html += '</div>';
                                html += '<div class="event-details">';
                                html += '<strong>' + evt.engine + '</strong> ';
                                if (evt.gate) html += '• <strong>' + evt.gate + '</strong> ';
                                if (evt.side) html += '• ' + evt.side + ' ';
                                if (evt.price > 0) html += '• $' + evt.price.toFixed(2) + ' ';
                                if (evt.size > 0) html += '• ' + evt.size.toFixed(4) + ' ';
                                if (evt.details) html += '• ' + evt.details;
                                html += '</div></div>';
                            });
                        } else {
                            html += '<div class="no-events">No recent events</div>';
                        }
                        
                        html += '</div></div>';
                    });
                    
                    document.getElementById('lanes').innerHTML = html;
                });
        }
        
        setInterval(update, 1000);
        update();
    </script>
</body>
</html>)";
    }
    
    int port_;
    std::atomic<bool> running_;
    int server_fd_;
    std::thread thread_;
    std::vector<LaneMetrics> metrics_;
    mutable std::mutex mutex_;
};
