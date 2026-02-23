#include <atomic>
#include <csignal>

#include <iostream>
#include <thread>
#include <string>
#include <cstring>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#endif
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <netinet/in.h>
#endif
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#endif
#include <sstream>
#include <iomanip>
#include <ctime>
#include <netdb.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <fstream>
#include <map>

namespace chimera {

// Configuration storage
struct FIXConfig {
    std::string host;
    int port;
    std::string sender_comp_id;
    std::string target_comp_id;
    std::string target_sub_id;
    std::string username;
    std::string password;
    int heartbeat_interval;
    std::string reset_seq_num;
    int dashboard_port;
};

static FIXConfig g_config;

// Simple INI parser
bool loadConfig(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << filename << "\n";
        return false;
    }
    
    std::string line, section;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;
        
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t");
        size_t end = line.find_last_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start, end - start + 1);
        
        // Section
        if (line[0] == '[' && line.back() == ']') {
            section = line.substr(1, line.length() - 2);
            continue;
        }
        
        // Key=value
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        
        // Trim key and value
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        
        // Store in config
        if (section == "fix") {
            if (key == "host") g_config.host = value;
            else if (key == "port") g_config.port = std::stoi(value);
            else if (key == "sender_comp_id") g_config.sender_comp_id = value;
            else if (key == "target_comp_id") g_config.target_comp_id = value;
            else if (key == "target_sub_id") g_config.target_sub_id = value;
            else if (key == "username") g_config.username = value;
            else if (key == "password") g_config.password = value;
            else if (key == "heartbeat_interval") g_config.heartbeat_interval = std::stoi(value);
            else if (key == "reset_seq_num") g_config.reset_seq_num = value;
        } else if (section == "dashboard") {
            if (key == "port") g_config.dashboard_port = std::stoi(value);
        }
    }
    
    return true;
}

// Global gold price from FIX feed
static std::atomic<double> g_gold_bid{2650.00};
static std::atomic<double> g_gold_ask{2650.00};
static std::atomic<bool> g_fix_connected{false};

class BlackBullFIX {
public:
    BlackBullFIX() : m_running(false), m_socket(-1), m_seq_num(1), m_ssl_ctx(nullptr), m_ssl(nullptr) {
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
    }
    
    ~BlackBullFIX() {
        stop();
        if (m_ssl_ctx) {
            SSL_CTX_free(m_ssl_ctx);
        }
    }
    
    void start() {
        m_running = true;
        m_thread = std::thread([this]() { run(); });
    }
    
    void stop() {
        if (!m_running) return;
        m_running = false;
        if (m_ssl) {
            SSL_shutdown(m_ssl);
            SSL_free(m_ssl);
            m_ssl = nullptr;
        }
        if (m_socket >= 0) {
            close(m_socket);
            m_socket = -1;
        }
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }
    
private:
    std::string getTimestamp() {
        // NO MILLISECONDS! This is critical for BlackBull
        auto now = std::time(nullptr);
        std::tm* gmt = std::gmtime(&now);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y%m%d-%H:%M:%S", gmt);
        return std::string(buf);
    }
    
    int calculateChecksum(const std::string& msg) {
        int sum = 0;
        for (unsigned char c : msg) {
            sum += c;
        }
        return sum % 256;
    }
    
    std::string buildLogon() {
        // Read from config.ini - NO MORE HARDCODED VALUES!
        std::string timestamp = getTimestamp();
        
        std::stringstream body;
        body << "35=A\x01"                                    // MsgType = Logon
             << "49=" << g_config.sender_comp_id << "\x01"    // SenderCompID from config
             << "56=" << g_config.target_comp_id << "\x01"    // TargetCompID from config
             << "34=" << m_seq_num++ << "\x01"                // MsgSeqNum
             << "52=" << timestamp << "\x01"                  // SendingTime
             << "57=" << g_config.target_sub_id << "\x01"     // TargetSubID from config
             << "98=0\x01"                                    // EncryptMethod
             << "108=" << g_config.heartbeat_interval << "\x01"  // HeartBtInt from config
             << "141=" << g_config.reset_seq_num << "\x01"    // ResetSeqNumFlag from config
             << "553=" << g_config.username << "\x01"         // Username from config
             << "554=" << g_config.password << "\x01";        // Password from config
        
        std::string body_str = body.str();
        int body_len = body_str.length();
        
        // Build complete message
        std::stringstream msg;
        msg << "8=FIX.4.4\x01"
            << "9=" << body_len << "\x01"
            << body_str;
        
        std::string msg_str = msg.str();
        int checksum = calculateChecksum(msg_str);
        
        msg << "10=" << std::setfill('0') << std::setw(3) << checksum << "\x01";
        
        return msg.str();
    }
    
    std::string buildMarketDataRequest() {
        std::string timestamp = getTimestamp();
        
        std::stringstream body;
        body << "35=V\x01"                                    // MarketDataRequest
             << "49=" << g_config.sender_comp_id << "\x01"    // SenderCompID from config
             << "56=" << g_config.target_comp_id << "\x01"    // TargetCompID from config
             << "34=" << m_seq_num++ << "\x01"                // MsgSeqNum
             << "52=" << timestamp << "\x01"                  // SendingTime
             << "57=" << g_config.target_sub_id << "\x01"     // TargetSubID from config
             << "262=XAUUSD_REQ\x01"                          // MDReqID
             << "263=1\x01"                                   // SubscriptionRequestType
             << "264=0\x01"                                   // MarketDepth
             << "146=1\x01"                                   // NoRelatedSym
             << "55=XAUUSD\x01"                               // Symbol
             << "267=2\x01"                                   // NoMDEntryTypes
             << "269=0\x01"                                   // Bid
             << "269=1\x01";                                  // Offer
        
        std::string body_str = body.str();
        int body_len = body_str.length();
        
        std::stringstream msg;
        msg << "8=FIX.4.4\x01"
            << "9=" << body_len << "\x01"
            << body_str;
        
        std::string msg_str = msg.str();
        int checksum = calculateChecksum(msg_str);
        
        msg << "10=" << std::setfill('0') << std::setw(3) << checksum << "\x01";
        
        return msg.str();
    }
    
    void parseMarketData(const std::string& msg) {
        // Parse FIX message for bid/ask prices
        // Format: ...269=0|270=PRICE|... (bid) ...269=1|270=PRICE|... (ask)
        
        size_t pos = 0;
        while (pos < msg.length()) {
            // Look for MDEntryType (269)
            size_t type_pos = msg.find("\x01""269=", pos);
            if (type_pos == std::string::npos) break;
            
            type_pos += 5; // Skip past "269="
            char entry_type = msg[type_pos];
            
            // Look for price (270) after this entry type
            size_t price_pos = msg.find("\x01""270=", type_pos);
            if (price_pos == std::string::npos) break;
            
            price_pos += 5; // Skip past "270="
            size_t price_end = msg.find('\x01', price_pos);
            if (price_end == std::string::npos) break;
            
            std::string price_str = msg.substr(price_pos, price_end - price_pos);
            double price = std::stod(price_str);
            
            if (entry_type == '0') {
                g_gold_bid.store(price);
            } else if (entry_type == '1') {
                g_gold_ask.store(price);
            }
            
            pos = price_end;
        }
    }
    
    bool connectToFIX() {
        // Create SSL context if not exists
        if (!m_ssl_ctx) {
            m_ssl_ctx = SSL_CTX_new(TLS_client_method());
            if (!m_ssl_ctx) {
                std::cerr << "[FIX] Failed to create SSL context\n";
                return false;
            }
        }
        
        struct hostent* host = gethostbyname(g_config.host.c_str());
        if (!host) {
            std::cerr << "[FIX] DNS lookup failed for " << g_config.host << "\n";
            return false;
        }
        
        m_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (m_socket < 0) {
            std::cerr << "[FIX] Socket creation failed\n";
            return false;
        }
        
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(g_config.port);
        memcpy(&addr.sin_addr, host->h_addr, host->h_length);
        
        if (connect(m_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "[FIX] TCP connection failed\n";
            close(m_socket);
            m_socket = -1;
            return false;
        }
        
        // Create SSL connection
        m_ssl = SSL_new(m_ssl_ctx);
        if (!m_ssl) {
            std::cerr << "[FIX] Failed to create SSL\n";
            close(m_socket);
            m_socket = -1;
            return false;
        }
        
        SSL_set_fd(m_ssl, m_socket);
        
        if (SSL_connect(m_ssl) <= 0) {
            std::cerr << "[FIX] SSL handshake failed\n";
            ERR_print_errors_fp(stderr);
            SSL_free(m_ssl);
            m_ssl = nullptr;
            close(m_socket);
            m_socket = -1;
            return false;
        }
        
        std::cout << "[FIX] SSL connected to " << g_config.host << ":" << g_config.port << "!\n";
        return true;
    }
    
    void run() {
        while (m_running) {
            std::cout << "[FIX] Connecting to " << g_config.host << ":" << g_config.port << "...\n";
            
            if (!connectToFIX()) {
                std::cerr << "[FIX] Failed to connect, retrying in 5s...\n";
                sleep(5);
                continue;
            }
            
            std::cout << "[FIX] Connected!\n";
            
            // Send logon
            std::string logon = buildLogon();
            int sent = SSL_write(m_ssl, logon.c_str(), logon.length());
            if (sent <= 0) {
                std::cerr << "[FIX] Logon send failed\n";
                SSL_free(m_ssl);
                m_ssl = nullptr;
                close(m_socket);
                m_socket = -1;
                sleep(5);
                continue;
            }
            
            std::cout << "[FIX] Logon sent, waiting for response...\n";
            
            // Read logon response
            char buffer[4096];
            int received = SSL_read(m_ssl, buffer, sizeof(buffer));
            if (received <= 0) {
                std::cerr << "[FIX] No logon response\n";
                SSL_free(m_ssl);
                m_ssl = nullptr;
                close(m_socket);
                m_socket = -1;
                sleep(5);
                continue;
            }
            
            std::string response(buffer, received);
            
            // Check for logon acceptance (35=A)
            if (response.find("\x01""35=A\x01") != std::string::npos) {
                std::cout << "[FIX] LOGON SUCCESSFUL!\n";
                g_fix_connected.store(true);
                
                // Subscribe to XAUUSD
                std::string mdReq = buildMarketDataRequest();
                int md_sent = SSL_write(m_ssl, mdReq.c_str(), mdReq.length());
                if (md_sent > 0) {
                    std::cout << "[FIX] Market data subscription sent for XAUUSD (sent " << md_sent << " bytes)\n";
                } else {
                    std::cerr << "[FIX] Failed to send market data subscription!\n";
                }
                
                // Read market data
                while (m_running) {
                    received = SSL_read(m_ssl, buffer, sizeof(buffer));
                    if (received <= 0) {
                        std::cerr << "[FIX] Connection lost\n";
                        g_fix_connected.store(false);
                        break;
                    }
                    
                    std::string msg(buffer, received);
                    
                    // DEBUG: Show what message type we got
                    size_t msg_type_pos = msg.find("\x01""35=");
                    if (msg_type_pos != std::string::npos) {
                        char msg_type = msg[msg_type_pos + 4];
                        std::cout << "[FIX] Received message type: " << msg_type << "\n";
                    }
                    
                    // Market Data Snapshot (35=W)
                    if (msg.find("\x01""35=W\x01") != std::string::npos) {
                        std::cout << "[FIX] Market data snapshot received!\n";
                        parseMarketData(msg);
                        double bid = g_gold_bid.load();
                        double ask = g_gold_ask.load();
                        std::cout << "[FIX] Gold: " << std::fixed << std::setprecision(2) 
                                  << bid << " / " << ask << "\n";
                    }
                    
                    // Heartbeat (35=0) - respond
                    if (msg.find("\x01""35=0\x01") != std::string::npos) {
                        std::cout << "[FIX] Heartbeat received, sending response\n";
                        // Echo heartbeat back
                        SSL_write(m_ssl, msg.c_str(), msg.length());
                    }
                }
            } else {
                std::cerr << "[FIX] Logon failed: " << response << "\n";
                g_fix_connected.store(false);
            }
            
            if (m_ssl) {
                SSL_shutdown(m_ssl);
                SSL_free(m_ssl);
                m_ssl = nullptr;
            }
            close(m_socket);
            m_socket = -1;
            
            if (m_running) {
                sleep(5);
            }
        }
    }
    
    std::atomic<bool> m_running;
    std::thread m_thread;
    int m_socket;
    int m_seq_num;
    SSL_CTX* m_ssl_ctx;
    SSL* m_ssl;
};

class TradingDashboard {
public:
    TradingDashboard(int port) : m_port(port), m_running(false), m_server_fd(-1) {}
    
    ~TradingDashboard() {
        stop();
    }
    
    void start() {
        m_running = true;
        m_thread = std::thread([this]() { run(); });
    }
    
    void stop() {
        if (!m_running) return;
        m_running = false;
        if (m_server_fd >= 0) {
            shutdown(m_server_fd, SHUT_RDWR);
            close(m_server_fd);
            m_server_fd = -1;
        }
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }
    
private:
    std::string getCurrentTime() {
        auto now = std::time(nullptr);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&now), "%H:%M:%S");
        return ss.str();
    }
    
    std::string buildDataJSON() {
        double bid = g_gold_bid.load();
        double ask = g_gold_ask.load();
        double mid = (bid + ask) / 2.0;
        bool connected = g_fix_connected.load();
        
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << "{\"time\":\"" << getCurrentTime() << "\""
           << ",\"price\":" << mid
           << ",\"bid\":" << bid
           << ",\"ask\":" << ask
           << ",\"spread\":" << (ask - bid)
           << ",\"connected\":" << (connected ? "true" : "false")
           << "}";
        return ss.str();
    }
    
    std::string buildDashboard() {
        double price = (g_gold_bid.load() + g_gold_ask.load()) / 2.0;
        std::stringstream priceStr;
        priceStr << std::fixed << std::setprecision(2) << price;
        
        return R"HTML(<!DOCTYPE html>
<html><head>
<title>Chimera v7.0 - LIVE</title>
<meta charset="UTF-8">
<link rel="icon" href="data:image/png;base64,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" type="image/png">
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body { 
    background: #f5f5f5;
    color: #000; 
    font-family: Arial, sans-serif; 
    padding: 20px;
    font-size: 20px;
    font-weight: 700;
}
.container { max-width: 1200px; margin: 0 auto; }
.header { 
    background: linear-gradient(135deg, #1976d2 0%, #0d47a1 100%);
    padding: 30px;
    border-radius: 12px;
    margin-bottom: 20px;
    text-align: center;
    box-shadow: 0 4px 15px rgba(0,0,0,0.3);
}
.header h1 { 
    color: #fff; 
    font-size: 48px; 
    margin-bottom: 10px; 
    font-weight: bold;
    text-shadow: 2px 2px 4px rgba(0,0,0,0.4);
}
.status {
    color: #fff;
    font-size: 20px;
    font-weight: bold;
}
.status.connected { color: #4caf50; }
.status.disconnected { color: #f44336; }
.price-panel {
    background: #fff;
    padding: 40px;
    border-radius: 12px;
    text-align: center;
    border: 4px solid #1976d2;
    margin-bottom: 20px;
    box-shadow: 0 4px 15px rgba(0,0,0,0.2);
}
.price-label {
    font-size: 24px;
    color: #666;
    margin-bottom: 15px;
}
.price-value {
    font-size: 96px;
    color: #2e7d32;
    font-weight: bold;
    font-family: 'Courier New', monospace;
    text-shadow: 2px 2px 4px rgba(0,0,0,0.1);
}
.price-details {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 20px;
    margin-top: 30px;
}
.price-detail {
    background: #f5f5f5;
    padding: 20px;
    border-radius: 8px;
    border: 2px solid #ddd;
}
.price-detail-label {
    font-size: 16px;
    color: #666;
    margin-bottom: 10px;
}
.price-detail-value {
    font-size: 36px;
    color: #000;
    font-weight: bold;
}
</style>
</head>
<body>
<div class="container">

<div class="header">
    <h1>CHIMERA v7.0 - LIVE GOLD</h1>
    <div id="status" class="status">??? CONNECTING...</div>
    <div style="color: #fff; margin-top: 10px; font-size: 16px;">BlackBull FIX ?? demo-uk-eqx-02.p.c-trader.com:5211</div>
</div>

<div class="price-panel">
    <div class="price-label">XAUUSD ?? LIVE MARKET PRICE</div>
    <div class="price-value" id="price">)HTML" + priceStr.str() + R"HTML(</div>
    <div class="price-details">
        <div class="price-detail">
            <div class="price-detail-label">BID</div>
            <div class="price-detail-value" id="bid">--</div>
        </div>
        <div class="price-detail">
            <div class="price-detail-label">ASK</div>
            <div class="price-detail-value" id="ask">--</div>
        </div>
        <div class="price-detail">
            <div class="price-detail-label">SPREAD</div>
            <div class="price-detail-value" id="spread">--</div>
        </div>
    </div>
</div>

</div>

<script>
function updateData() {
    fetch('/data')
        .then(r => r.json())
        .then(d => {
            document.getElementById('price').textContent = d.price.toFixed(2);
            document.getElementById('bid').textContent = d.bid.toFixed(2);
            document.getElementById('ask').textContent = d.ask.toFixed(2);
            document.getElementById('spread').textContent = d.spread.toFixed(2);
            
            const statusEl = document.getElementById('status');
            if (d.connected) {
                statusEl.className = 'status connected';
                statusEl.textContent = '??? LIVE - FIX CONNECTED';
            } else {
                statusEl.className = 'status disconnected';
                statusEl.textContent = '??? DISCONNECTED';
            }
        })
        .catch(e => console.log('Update failed:', e));
}

setInterval(updateData, 1000);
updateData();
</script>
</body>
</html>)HTML";
    }
    
    void run() {
        m_server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (m_server_fd < 0) {
            std::cerr << "[GUI] Failed to create socket\n";
            return;
        }
        
        int opt = 1;
        setsockopt(m_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(m_port);
        
        if (bind(m_server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "[GUI] Failed to bind to port " << m_port << "\n";
            close(m_server_fd);
            m_server_fd = -1;
            return;
        }
        
        listen(m_server_fd, 5);
        std::cout << "[GUI] Dashboard server listening on port " << m_port << "\n";
        
        while (m_running) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(m_server_fd, &readfds);
            
            timeval tv{1, 0};
            int activity = select(m_server_fd + 1, &readfds, nullptr, nullptr, &tv);
            
            if (activity < 0 && m_running) break;
            if (activity == 0) continue;
            if (!m_running) break;
            
            int client_fd = accept(m_server_fd, nullptr, nullptr);
            if (client_fd < 0) {
                if (m_running) std::cerr << "[GUI] Accept failed\n";
                break;
            }
            
            char buffer[4096] = {0};
            ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
            (void)bytes_read;
            
            std::string request(buffer);
            std::string response;
            
            if (request.find("GET /data") != std::string::npos) {
                std::string json = buildDataJSON();
                response = 
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json\r\n"
                    "Connection: close\r\n\r\n" + json;
            } else {
                std::string html = buildDashboard();
                response = 
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html; charset=UTF-8\r\n"
                    "Connection: close\r\n\r\n" + html;
            }
            
            ssize_t bytes_written = write(client_fd, response.c_str(), response.length());
            (void)bytes_written;
            
            close(client_fd);
        }
        
        std::cout << "[GUI] Dashboard server stopped\n";
    }
    
    int m_port;
    std::atomic<bool> m_running;
    std::thread m_thread;
    int m_server_fd;
};

}

static std::atomic<bool> run(true);

void sig(int){ 
    std::cout << "\n[SIGNAL] Received shutdown signal\n";
    run = false; 
}

int main(int argc, char* argv[]) {
    signal(SIGINT, sig);
    signal(SIGTERM, sig);
    
    // Load configuration
    std::string config_file = (argc > 1) ? argv[1] : "config.ini";
    if (!chimera::loadConfig(config_file)) {
        std::cerr << "Failed to load config from: " << config_file << "\n";
        std::cerr << "Usage: " << argv[0] << " [config.ini]\n";
        return 1;
    }
    
    std::cout << "?????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????\n";
    std::cout << "  CHIMERA v7.0 - LIVE Gold Trading\n";
    std::cout << "  Configuration loaded from: " << config_file << "\n";
    std::cout << "?????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????\n";
    std::cout << "  FIX Settings:\n";
    std::cout << "  Host: " << chimera::g_config.host << ":" << chimera::g_config.port << "\n";
    std::cout << "  SenderCompID: " << chimera::g_config.sender_comp_id << "\n";
    std::cout << "  TargetCompID: " << chimera::g_config.target_comp_id << "\n";
    std::cout << "  TargetSubID: " << chimera::g_config.target_sub_id << "\n";
    std::cout << "  Username: " << chimera::g_config.username << "\n";
    std::cout << "  Password: " << chimera::g_config.password << "\n";
    std::cout << "  Dashboard: http://185.167.119.59:" << chimera::g_config.dashboard_port << "\n";
    std::cout << "?????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????\n\n";
    
    chimera::BlackBullFIX fix;
    fix.start();
    
    chimera::TradingDashboard dashboard(chimera::g_config.dashboard_port);
    dashboard.start();
    
    std::cout << "[CHIMERA] Press Ctrl+C to stop\n\n";
    
    while (run) {
        sleep(1);
    }
    
    std::cout << "[CHIMERA] Shutting down...\n";
    fix.stop();
    dashboard.stop();
    std::cout << "[CHIMERA] Shutdown complete\n";
    
    return 0;
}

