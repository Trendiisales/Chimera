// ═══════════════════════════════════════════════════════════════════════════════
// Alpha Trading System - WebSocket Dashboard Server
// ═══════════════════════════════════════════════════════════════════════════════
// VERSION: 1.0.0
// PURPOSE: Serve real-time data to the dashboard HTML
// PROTOCOL: Simple JSON over WebSocket (port 8080)
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include "core/Types.hpp"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <functional>
#include <fstream>

// Platform includes
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
    #define CLOSE_SOCKET(s) closesocket(s)
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <arpa/inet.h>
    #define SOCKET int
    #define CLOSE_SOCKET(s) close(s)
    #define INVALID_SOCKET -1
#endif

#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

namespace Alpha {

// ═══════════════════════════════════════════════════════════════════════════════
// SIMPLE BASE64 ENCODER
// ═══════════════════════════════════════════════════════════════════════════════
inline std::string base64_encode(const unsigned char* data, size_t len) {
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(((len + 2) / 3) * 4);
    
    for (size_t i = 0; i < len; i += 3) {
        unsigned int n = (data[i] << 16);
        if (i + 1 < len) n |= (data[i + 1] << 8);
        if (i + 2 < len) n |= data[i + 2];
        
        result += chars[(n >> 18) & 0x3F];
        result += chars[(n >> 12) & 0x3F];
        result += (i + 1 < len) ? chars[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? chars[n & 0x3F] : '=';
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// DASHBOARD DATA STRUCTURES
// ═══════════════════════════════════════════════════════════════════════════════

struct EngineStatus {
    Instrument instrument = Instrument::INVALID;
    EngineState state = EngineState::INIT;
    
    // Prices
    double bid = 0, ask = 0, spread = 0;
    
    // Stats
    uint64_t ticks = 0;
    int trades = 0;
    double pnl_bps = 0;
    
    // Session
    std::string session = "OFF";
    std::string session_type = "off";
    double multiplier = 0;
    
    // Expectancy
    double expectancy = 0;
    double win_rate = 0;
    
    // Regime
    std::string regime = "QUIET";
    double momentum = 0;
    double volatility = 0;
    double spread_bps = 0;
    
    // Position
    bool has_position = false;
    std::string pos_side = "FLAT";
    double pos_entry = 0;
    double pos_size = 0;
    double pos_pnl_bps = 0;
    uint64_t pos_hold_ms = 0;
    bool pos_trailing = false;
    
    std::string to_json() const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << "{";
        ss << "\"state\":\"" << engine_state_str(state) << "\",";
        ss << "\"bid\":" << bid << ",";
        ss << "\"ask\":" << ask << ",";
        ss << "\"spread\":" << spread << ",";
        ss << "\"ticks\":" << ticks << ",";
        ss << "\"trades\":" << trades << ",";
        ss << "\"pnl\":" << std::setprecision(1) << pnl_bps << ",";
        ss << "\"session\":\"" << session << "\",";
        ss << "\"session_type\":\"" << session_type << "\",";
        ss << "\"multiplier\":" << std::setprecision(1) << multiplier << ",";
        ss << "\"expectancy\":" << expectancy << ",";
        ss << "\"win_rate\":" << std::setprecision(2) << win_rate << ",";
        ss << "\"regime\":\"" << regime << "\",";
        ss << "\"momentum\":" << momentum << ",";
        ss << "\"volatility\":" << volatility << ",";
        ss << "\"spread_bps\":" << spread_bps << ",";
        ss << "\"position\":{";
        ss << "\"side\":\"" << pos_side << "\",";
        ss << "\"entry\":" << std::setprecision(2) << pos_entry << ",";
        ss << "\"size\":" << pos_size << ",";
        ss << "\"pnl_bps\":" << std::setprecision(1) << pos_pnl_bps << ",";
        ss << "\"hold_time\":" << pos_hold_ms << ",";
        ss << "\"trailing\":" << (pos_trailing ? "true" : "false");
        ss << "}";
        ss << "}";
        return ss.str();
    }
};

struct DashboardStatus {
    uint64_t uptime_s = 0;
    uint64_t total_ticks = 0;
    int total_trades = 0;
    double total_pnl = 0;
    int daily_trades = 0;
    double daily_pnl = 0;
    
    EngineStatus xauusd;
    EngineStatus nas100;
    
    std::string to_json() const {
        std::ostringstream ss;
        ss << std::fixed;
        ss << "{\"type\":\"status\",";
        ss << "\"uptime\":" << uptime_s << ",";
        ss << "\"total_ticks\":" << total_ticks << ",";
        ss << "\"total_trades\":" << total_trades << ",";
        ss << "\"total_pnl\":" << std::setprecision(1) << total_pnl << ",";
        ss << "\"daily_trades\":" << daily_trades << ",";
        ss << "\"daily_pnl\":" << std::setprecision(2) << daily_pnl << ",";
        ss << "\"xauusd\":" << xauusd.to_json() << ",";
        ss << "\"nas100\":" << nas100.to_json();
        ss << "}";
        return ss.str();
    }
};

struct TradeRecord {
    uint64_t timestamp = 0;
    std::string instrument;
    std::string side;
    double size = 0;
    double entry = 0;
    double exit_price = 0;
    double pnl_bps = 0;
    std::string reason;
    
    std::string to_json() const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << "{\"type\":\"trade\",";
        ss << "\"time\":" << timestamp << ",";
        ss << "\"instrument\":\"" << instrument << "\",";
        ss << "\"side\":\"" << side << "\",";
        ss << "\"size\":" << size << ",";
        ss << "\"entry\":" << entry << ",";
        ss << "\"exit\":" << exit_price << ",";
        ss << "\"pnl\":" << std::setprecision(1) << pnl_bps << ",";
        ss << "\"reason\":\"" << reason << "\"";
        ss << "}";
        return ss.str();
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// WEBSOCKET DASHBOARD SERVER
// ═══════════════════════════════════════════════════════════════════════════════

class DashboardServer {
public:
    DashboardServer(uint16_t port = 8080) noexcept 
        : port_(port), running_(false), server_socket_(INVALID_SOCKET) {}
    
    ~DashboardServer() {
        stop();
    }
    
    bool start() noexcept {
        if (running_.load()) return false;
        
#ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "[Dashboard] WSAStartup failed\n";
            return false;
        }
#endif
        
        server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket_ == INVALID_SOCKET) {
            std::cerr << "[Dashboard] Failed to create socket\n";
            return false;
        }
        
        int opt = 1;
        setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
        
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);
        
        if (bind(server_socket_, (sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "[Dashboard] Failed to bind port " << port_ << "\n";
            CLOSE_SOCKET(server_socket_);
            return false;
        }
        
        if (listen(server_socket_, 5) < 0) {
            std::cerr << "[Dashboard] Failed to listen\n";
            CLOSE_SOCKET(server_socket_);
            return false;
        }
        
        running_.store(true);
        accept_thread_ = std::thread(&DashboardServer::accept_loop, this);
        
        std::cout << "[Dashboard] Server started on port " << port_ << "\n";
        return true;
    }
    
    void stop() noexcept {
        if (!running_.load()) return;
        
        running_.store(false);
        
        // Close all client sockets
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            for (SOCKET client : clients_) {
                CLOSE_SOCKET(client);
            }
            clients_.clear();
        }
        
        // Close server socket
        if (server_socket_ != INVALID_SOCKET) {
            CLOSE_SOCKET(server_socket_);
            server_socket_ = INVALID_SOCKET;
        }
        
        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }
        
#ifdef _WIN32
        WSACleanup();
#endif
        
        std::cout << "[Dashboard] Server stopped\n";
    }
    
    void broadcast(const std::string& json) noexcept {
        if (!running_.load()) return;
        
        std::vector<uint8_t> frame = make_ws_frame(json);
        
        std::lock_guard<std::mutex> lock(clients_mutex_);
        std::vector<SOCKET> dead_clients;
        
        for (SOCKET client : clients_) {
            if (send(client, (const char*)frame.data(), (int)frame.size(), 0) <= 0) {
                dead_clients.push_back(client);
            }
        }
        
        // Remove dead clients
        for (SOCKET dead : dead_clients) {
            CLOSE_SOCKET(dead);
            clients_.erase(std::remove(clients_.begin(), clients_.end(), dead), clients_.end());
        }
    }
    
    void broadcast_status(const DashboardStatus& status) noexcept {
        broadcast(status.to_json());
    }
    
    void broadcast_trade(const TradeRecord& trade) noexcept {
        broadcast(trade.to_json());
    }
    
    [[nodiscard]] bool is_running() const noexcept { return running_.load(); }
    [[nodiscard]] size_t client_count() const noexcept {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        return clients_.size();
    }

private:
    void accept_loop() noexcept {
        while (running_.load()) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(server_socket_, &readfds);
            
            timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            
            int result = select((int)server_socket_ + 1, &readfds, nullptr, nullptr, &tv);
            if (result <= 0) continue;
            
            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            SOCKET client = accept(server_socket_, (sockaddr*)&client_addr, &client_len);
            
            if (client == INVALID_SOCKET) continue;
            
            // Handle WebSocket handshake in separate thread
            std::thread([this, client]() {
                if (handle_handshake(client)) {
                    std::lock_guard<std::mutex> lock(clients_mutex_);
                    clients_.push_back(client);
                    std::cout << "[Dashboard] Client connected (total: " << clients_.size() << ")\n";
                } else {
                    CLOSE_SOCKET(client);
                }
            }).detach();
        }
    }
    
    bool handle_handshake(SOCKET client) noexcept {
        char buffer[4096];
        int received = recv(client, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) return false;
        buffer[received] = '\0';
        
        std::string request(buffer);
        
        // Check if this is a WebSocket upgrade request
        size_t key_pos = request.find("Sec-WebSocket-Key: ");
        
        if (key_pos == std::string::npos) {
            // Regular HTTP request - serve the dashboard HTML
            if (request.find("GET ") == 0) {
                serve_http(client, request);
            }
            return false;  // Don't add to WebSocket clients
        }
        
        // WebSocket upgrade
        key_pos += 19;
        size_t key_end = request.find("\r\n", key_pos);
        if (key_end == std::string::npos) return false;
        
        std::string key = request.substr(key_pos, key_end - key_pos);
        
        // Generate accept key
        std::string accept_key = generate_accept_key(key);
        
        // Send handshake response
        std::ostringstream response;
        response << "HTTP/1.1 101 Switching Protocols\r\n";
        response << "Upgrade: websocket\r\n";
        response << "Connection: Upgrade\r\n";
        response << "Sec-WebSocket-Accept: " << accept_key << "\r\n";
        response << "\r\n";
        
        std::string resp = response.str();
        return send(client, resp.c_str(), (int)resp.length(), 0) > 0;
    }
    
    void serve_http(SOCKET client, const std::string& request) noexcept {
        // Extract path
        size_t path_start = request.find("GET ") + 4;
        size_t path_end = request.find(" HTTP");
        std::string path = request.substr(path_start, path_end - path_start);
        
        // Serve dashboard HTML
        if (path == "/" || path == "/alpha_dashboard.html" || path.find("dashboard") != std::string::npos) {
            std::string html = load_dashboard_file();
            
            std::ostringstream response;
            response << "HTTP/1.1 200 OK\r\n";
            response << "Content-Type: text/html\r\n";
            response << "Content-Length: " << html.length() << "\r\n";
            response << "Connection: close\r\n";
            response << "\r\n";
            response << html;
            
            std::string resp = response.str();
            send(client, resp.c_str(), (int)resp.length(), 0);
        } else {
            // 404
            std::string body = "<html><body><h1>404 Not Found</h1></body></html>";
            std::ostringstream response;
            response << "HTTP/1.1 404 Not Found\r\n";
            response << "Content-Type: text/html\r\n";
            response << "Content-Length: " << body.length() << "\r\n";
            response << "Connection: close\r\n";
            response << "\r\n";
            response << body;
            
            std::string resp = response.str();
            send(client, resp.c_str(), (int)resp.length(), 0);
        }
        
        CLOSE_SOCKET(client);
    }
    
    std::string load_dashboard_file() noexcept {
        // Try multiple paths
        const char* paths[] = {
            "alpha_dashboard.html",
            "./alpha_dashboard.html",
            "../alpha_dashboard.html",
            "/mnt/c/Alpha/alpha_dashboard.html"
        };
        
        for (const char* path : paths) {
            std::ifstream file(path);
            if (file.is_open()) {
                std::ostringstream ss;
                ss << file.rdbuf();
                std::cout << "[Dashboard] Loaded HTML from: " << path << "\n";
                return ss.str();
            }
        }
        
        std::cerr << "[Dashboard] WARNING: Could not find alpha_dashboard.html, using fallback\n";
        return get_fallback_html();
    }
    
    std::string get_fallback_html() noexcept {
        return R"HTML(<!DOCTYPE html>
<html><head><title>Alpha Dashboard</title>
<style>body{background:#0a0a0f;color:#e0e0e0;font-family:monospace;padding:20px;}
h1{color:#ffd700;}.error{color:#ff6666;}</style></head>
<body><h1>Alpha Trading System</h1>
<p class="error">Dashboard HTML file not found. Place alpha_dashboard.html in the working directory.</p>
<p>WebSocket connecting to ws://localhost:8080</p>
<script>
let ws = new WebSocket('ws://' + location.host);
ws.onmessage = e => { document.body.innerHTML += '<pre>' + e.data + '</pre>'; };
ws.onerror = () => { document.body.innerHTML += '<p class="error">WebSocket error</p>'; };
</script></body></html>)HTML";
    }
    
    
    std::string generate_accept_key(const std::string& client_key) noexcept {
        std::string magic = client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        
        unsigned char hash[SHA_DIGEST_LENGTH];
        SHA1((const unsigned char*)magic.c_str(), magic.length(), hash);
        
        return base64_encode(hash, SHA_DIGEST_LENGTH);
    }
    
    std::vector<uint8_t> make_ws_frame(const std::string& data) noexcept {
        std::vector<uint8_t> frame;
        size_t len = data.length();
        
        frame.push_back(0x81);  // FIN + text frame
        
        if (len <= 125) {
            frame.push_back((uint8_t)len);
        } else if (len <= 65535) {
            frame.push_back(126);
            frame.push_back((len >> 8) & 0xFF);
            frame.push_back(len & 0xFF);
        } else {
            frame.push_back(127);
            for (int i = 7; i >= 0; --i) {
                frame.push_back((len >> (i * 8)) & 0xFF);
            }
        }
        
        frame.insert(frame.end(), data.begin(), data.end());
        return frame;
    }
    
    uint16_t port_;
    std::atomic<bool> running_;
    SOCKET server_socket_;
    std::thread accept_thread_;
    
    mutable std::mutex clients_mutex_;
    std::vector<SOCKET> clients_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// GLOBAL INSTANCE
// ═══════════════════════════════════════════════════════════════════════════════

inline DashboardServer& getDashboardServer() noexcept {
    static DashboardServer instance(8080);
    return instance;
}

}  // namespace Alpha
