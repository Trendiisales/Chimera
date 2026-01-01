// =============================================================================
// fix_diag.cpp - Standalone FIX Connectivity Diagnostic
// =============================================================================
// Reads all configuration from config.ini - NO HARDCODED VALUES
// Build: g++ -std=c++20 -o fix_diag fix_diag.cpp -lssl -lcrypto
// Run: ./fix_diag
// =============================================================================

#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <vector>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

constexpr char SOH = '\x01';

// =============================================================================
// SIMPLE CONFIG PARSER (reads config.ini)
// =============================================================================
class SimpleConfig {
public:
    bool load(const std::string& filename = "config.ini") {
        std::vector<std::string> paths = {
            filename,
            "../config.ini",
            "../../config.ini"
        };
        
        for (const auto& path : paths) {
            std::ifstream file(path);
            if (file.is_open()) {
                std::cout << "[Config] Loaded: " << path << "\n";
                return parse(file);
            }
        }
        
        std::cerr << "[Config] ERROR: config.ini not found!\n";
        return false;
    }
    
    std::string get(const std::string& section, const std::string& key, const std::string& defaultVal = "") const {
        std::string fullKey = section + "." + key;
        auto it = values_.find(fullKey);
        return (it != values_.end()) ? it->second : defaultVal;
    }
    
    int getInt(const std::string& section, const std::string& key, int defaultVal = 0) const {
        std::string val = get(section, key);
        if (val.empty()) return defaultVal;
        try { return std::stoi(val); } catch (...) { return defaultVal; }
    }

private:
    bool parse(std::ifstream& file) {
        std::string line, currentSection;
        
        while (std::getline(file, line)) {
            // Trim
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            line = line.substr(start);
            size_t end = line.find_last_not_of(" \t\r\n");
            if (end != std::string::npos) line = line.substr(0, end + 1);
            
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            
            if (line[0] == '[') {
                size_t closePos = line.find(']');
                if (closePos != std::string::npos) {
                    currentSection = line.substr(1, closePos - 1);
                }
                continue;
            }
            
            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos) {
                std::string key = line.substr(0, eqPos);
                std::string value = line.substr(eqPos + 1);
                
                // Trim key and value
                end = key.find_last_not_of(" \t");
                if (end != std::string::npos) key = key.substr(0, end + 1);
                start = value.find_first_not_of(" \t");
                if (start != std::string::npos) value = value.substr(start);
                end = value.find_last_not_of(" \t\r\n");
                if (end != std::string::npos) value = value.substr(0, end + 1);
                
                values_[currentSection + "." + key] = value;
            }
        }
        return !values_.empty();
    }
    
    std::unordered_map<std::string, std::string> values_;
};

// =============================================================================
// GLOBALS - loaded from config.ini
// =============================================================================
std::string g_host;
int g_port;
std::string g_sender_comp_id;
std::string g_target_comp_id;
std::string g_username;
std::string g_password;

std::string getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    gmtime_r(&time_t_now, &tm_now);
    
    char buf[64];  // Increased from 32 to 64 to avoid truncation warning
    std::snprintf(buf, sizeof(buf), 
        "%04d%02d%02d-%02d:%02d:%02d",
        tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
        tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
    return std::string(buf);
}

std::string buildLogon() {
    std::string body;
    char buf[64];
    int len;
    
    // 35=A (MsgType - Logon)
    body += "35=A";
    body += SOH;
    
    // 49=SenderCompID
    len = std::snprintf(buf, sizeof(buf), "49=%s%c", g_sender_comp_id.c_str(), SOH);
    body.append(buf, len);
    
    // 56=TargetCompID
    len = std::snprintf(buf, sizeof(buf), "56=%s%c", g_target_comp_id.c_str(), SOH);
    body.append(buf, len);
    
    // 34=MsgSeqNum
    body += "34=1";
    body += SOH;
    
    // 52=SendingTime
    std::string ts = getTimestamp();
    len = std::snprintf(buf, sizeof(buf), "52=%s%c", ts.c_str(), SOH);
    body.append(buf, len);
    
    // 57=TargetSubID (TRADE) - REQUIRED per Dec 16 working message
    body += "57=TRADE";
    body += SOH;
    
    // 50=SenderSubID (TRADE)
    body += "50=TRADE";
    body += SOH;
    
    // 98=EncryptMethod
    body += "98=0";
    body += SOH;
    
    // 108=HeartBtInt
    body += "108=30";
    body += SOH;
    
    // 141=ResetSeqNumFlag
    body += "141=Y";
    body += SOH;
    
    // 553=Username (numeric account ID)
    len = std::snprintf(buf, sizeof(buf), "553=%s%c", g_username.c_str(), SOH);
    body.append(buf, len);
    
    // 554=Password
    len = std::snprintf(buf, sizeof(buf), "554=%s%c", g_password.c_str(), SOH);
    body.append(buf, len);
    
    // Build final message
    std::string msg;
    msg += "8=FIX.4.4";
    msg += SOH;
    
    len = std::snprintf(buf, sizeof(buf), "9=%zu%c", body.length(), SOH);
    msg.append(buf, len);
    
    msg += body;
    
    // Checksum
    uint32_t checksum = 0;
    for (char c : msg) {
        checksum += static_cast<uint8_t>(c);
    }
    checksum %= 256;
    
    len = std::snprintf(buf, sizeof(buf), "10=%03u%c", checksum, SOH);
    msg.append(buf, len);
    
    return msg;
}

std::string displayable(const std::string& msg) {
    std::string result = msg;
    for (char& c : result) {
        if (c == SOH) c = '|';
    }
    return result;
}

int main() {
    // Load configuration from config.ini
    SimpleConfig cfg;
    if (!cfg.load()) {
        std::cerr << "Failed to load config.ini\n";
        return 1;
    }
    
    // Read all values from config.ini
    g_host = cfg.get("ctrader", "host");
    g_port = cfg.getInt("ctrader", "trade_port", 5212);
    g_sender_comp_id = cfg.get("ctrader", "sender_comp_id");
    g_target_comp_id = cfg.get("ctrader", "target_comp_id", "cServer");
    g_username = cfg.get("ctrader", "username");
    g_password = cfg.get("ctrader", "password");
    
    // Validate
    if (g_host.empty() || g_sender_comp_id.empty() || g_username.empty() || g_password.empty()) {
        std::cerr << "ERROR: Missing required config values in config.ini\n";
        std::cerr << "  host=" << g_host << "\n";
        std::cerr << "  sender_comp_id=" << g_sender_comp_id << "\n";
        std::cerr << "  username=" << g_username << "\n";
        std::cerr << "  password=" << (g_password.empty() ? "(empty)" : "****") << "\n";
        return 1;
    }
    
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  FIX CONNECTIVITY DIAGNOSTIC\n";
    std::cout << "  (All values from config.ini)\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  Host: " << g_host << "\n";
    std::cout << "  Port: " << g_port << " (TRADE)\n";
    std::cout << "  SenderCompID: " << g_sender_comp_id << "\n";
    std::cout << "  Username: " << g_username << "\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n\n";
    
    // Initialize OpenSSL
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    
    // Create socket
    std::cout << "[1] Creating socket...\n";
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        std::cerr << "    FAILED: socket() errno=" << errno << "\n";
        return 1;
    }
    std::cout << "    OK\n";
    
    // Set TCP_NODELAY
    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    
    // Resolve hostname
    std::cout << "[2] Resolving hostname...\n";
    struct addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    int rv = getaddrinfo(g_host.c_str(), std::to_string(g_port).c_str(), &hints, &result);
    if (rv != 0 || !result) {
        std::cerr << "    FAILED: getaddrinfo() rv=" << rv << "\n";
        close(sock);
        return 1;
    }
    
    // Print resolved IP
    char ipstr[INET_ADDRSTRLEN];
    struct sockaddr_in* addr = (struct sockaddr_in*)result->ai_addr;
    inet_ntop(AF_INET, &addr->sin_addr, ipstr, sizeof(ipstr));
    std::cout << "    OK: " << ipstr << "\n";
    
    // Connect
    std::cout << "[3] TCP connect...\n";
    if (connect(sock, result->ai_addr, result->ai_addrlen) != 0) {
        std::cerr << "    FAILED: connect() errno=" << errno << "\n";
        freeaddrinfo(result);
        close(sock);
        return 1;
    }
    freeaddrinfo(result);
    std::cout << "    OK\n";
    
    // Create SSL context
    std::cout << "[4] SSL context...\n";
    const SSL_METHOD* method = TLS_client_method();
    SSL_CTX* ctx = SSL_CTX_new(method);
    if (!ctx) {
        std::cerr << "    FAILED: SSL_CTX_new()\n";
        close(sock);
        return 1;
    }
    
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
    std::cout << "    OK\n";
    
    // Create SSL object
    std::cout << "[5] SSL object...\n";
    SSL* ssl = SSL_new(ctx);
    if (!ssl) {
        std::cerr << "    FAILED: SSL_new()\n";
        SSL_CTX_free(ctx);
        close(sock);
        return 1;
    }
    
    SSL_set_fd(ssl, sock);
    SSL_set_tlsext_host_name(ssl, g_host.c_str());
    std::cout << "    OK\n";
    
    // SSL handshake
    std::cout << "[6] SSL handshake...\n";
    int ret = SSL_connect(ssl);
    if (ret != 1) {
        int err = SSL_get_error(ssl, ret);
        char errbuf[256];
        ERR_error_string_n(ERR_get_error(), errbuf, sizeof(errbuf));
        std::cerr << "    FAILED: SSL_connect() err=" << err << " - " << errbuf << "\n";
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(sock);
        return 1;
    }
    std::cout << "    OK: " << SSL_get_cipher(ssl) << "\n";
    std::cout << "    Protocol: " << SSL_get_version(ssl) << "\n";
    
    // Build and send LOGON
    std::string logon = buildLogon();
    std::cout << "\n[7] Sending LOGON (" << logon.length() << " bytes)...\n";
    std::cout << "    " << displayable(logon) << "\n";
    
    int sent = SSL_write(ssl, logon.data(), logon.length());
    if (sent != (int)logon.length()) {
        std::cerr << "    FAILED: SSL_write() sent=" << sent << "\n";
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(sock);
        return 1;
    }
    std::cout << "    OK: sent " << sent << " bytes\n";
    
    // Wait for response with timeout
    std::cout << "\n[8] Waiting for response (30 seconds timeout)...\n";
    
    // Set socket to non-blocking for poll
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    
    char buffer[4096];
    auto start = std::chrono::steady_clock::now();
    int totalRecv = 0;
    std::string response;
    
    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count();
        
        if (elapsed >= 30) {
            std::cout << "\n    TIMEOUT: No response received in 30 seconds\n";
            break;
        }
        
        // Check if data available
        struct pollfd pfd;
        pfd.fd = sock;
        pfd.events = POLLIN;
        
        int pollRet = poll(&pfd, 1, 1000);  // 1 second timeout
        if (pollRet < 0) {
            std::cerr << "    ERROR: poll() errno=" << errno << "\n";
            break;
        }
        
        if (pollRet == 0) {
            std::cout << "    Still waiting... (" << elapsed << "s)\n";
            continue;
        }
        
        if (pfd.revents & POLLIN) {
            int n = SSL_read(ssl, buffer, sizeof(buffer) - 1);
            if (n > 0) {
                buffer[n] = '\0';
                response.append(buffer, n);
                totalRecv += n;
                
                std::cout << "\n    RECEIVED " << n << " bytes:\n";
                std::cout << "    " << displayable(std::string(buffer, n)) << "\n";
                
                // Check for complete message (ends with checksum)
                if (response.find("10=") != std::string::npos) {
                    std::cout << "\n    Complete FIX message received!\n";
                    
                    // Parse message type
                    size_t pos = response.find("35=");
                    if (pos != std::string::npos) {
                        char msgType = response[pos + 3];
                        std::cout << "    MsgType: " << msgType;
                        if (msgType == 'A') {
                            std::cout << " (LOGON - SUCCESS!)\n";
                        } else if (msgType == '5') {
                            std::cout << " (LOGOUT - REJECTED)\n";
                            // Find reason
                            size_t textPos = response.find("58=");
                            if (textPos != std::string::npos) {
                                size_t endPos = response.find(SOH, textPos);
                                std::cout << "    Reason: " << response.substr(textPos + 3, endPos - textPos - 3) << "\n";
                            }
                        } else if (msgType == '3') {
                            std::cout << " (REJECT)\n";
                        } else {
                            std::cout << "\n";
                        }
                    }
                    break;
                }
            } else if (n == 0) {
                std::cout << "    Connection closed by peer\n";
                break;
            } else {
                int err = SSL_get_error(ssl, n);
                if (err == SSL_ERROR_WANT_READ) {
                    continue;
                }
                std::cerr << "    ERROR: SSL_read() err=" << err << "\n";
                break;
            }
        }
        
        if (pfd.revents & (POLLERR | POLLHUP)) {
            std::cout << "    Socket error or hangup\n";
            break;
        }
    }
    
    // Cleanup
    std::cout << "\n[9] Cleanup...\n";
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close(sock);
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  SUMMARY\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  Bytes sent: " << logon.length() << "\n";
    std::cout << "  Bytes received: " << totalRecv << "\n";
    
    if (totalRecv > 0 && response.find("35=A") != std::string::npos) {
        std::cout << "  RESULT: SUCCESS - LOGON ACCEPTED\n";
    } else if (totalRecv > 0) {
        std::cout << "  RESULT: FAILED - Server responded but did not accept LOGON\n";
    } else {
        std::cout << "  RESULT: FAILED - No response from server\n";
        std::cout << "\n  POSSIBLE CAUSES:\n";
        std::cout << "  1. FIX API may be disabled on account " << g_username << "\n";
        std::cout << "  2. Account may be suspended or expired\n";
        std::cout << "  3. Server may be rejecting silently (firewall/rate limit)\n";
        std::cout << "  4. Weekend/maintenance - cTrader demo may be down\n";
        std::cout << "  5. Try alternate server: edit config.ini host value\n";
        std::cout << "\n  ACTION: Contact BlackBull Markets support to verify\n";
        std::cout << "          FIX API is enabled on your account.\n";
    }
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    
    return 0;
}
