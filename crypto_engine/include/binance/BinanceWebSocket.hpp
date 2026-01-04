// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/binance/BinanceWebSocket.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: SSL WebSocket connection to Binance streams
// OWNER: Jo
// LAST VERIFIED: 2026-01-03
//
// v4.9.23: PROPER SELECT/POLL GATING
// ═══════════════════════════════════════════════════════════════════════════════
//   The v4.9.22 non-blocking fix wasn't enough - still needed FD readability check
//   before calling SSL_read() to prevent:
//     - Blocking (if socket somehow becomes blocking)
//     - CPU spin (tight loop with WANT_READ)
//   
//   Now uses:
//     - Windows: select() with timeout
//     - Linux: poll() with timeout (more efficient)
//   
//   Only calls SSL_read() when socket is CONFIRMED readable.
//   Drains SSL buffer completely in a loop.
//
// v4.9.22: NON-BLOCKING SOCKET
//   Socket set to non-blocking mode after handshake completes
//
// DESIGN:
// - Non-blocking SSL socket with proper FD gating
// - WebSocket frame parsing (RFC 6455)
// - Reconnection handling
// - Ping/pong keepalive
// - Single-threaded (owned by connection thread)
//
// DEPENDENCIES:
// - OpenSSL (SSL_*, BIO_*)
// - System sockets (socket, connect, etc.)
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <functional>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    using socket_t = SOCKET;
    constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
    #define SOCK_CLOSE closesocket
    #define SOCK_ERRNO WSAGetLastError()
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    #include <poll.h>  // v4.9.23: For proper FD readability gating
    using socket_t = int;
    constexpr socket_t INVALID_SOCK = -1;
    #define SOCK_CLOSE close
    #define SOCK_ERRNO errno
#endif

#include <openssl/ssl.h>
#include <openssl/err.h>

namespace Chimera {
namespace Binance {

// ─────────────────────────────────────────────────────────────────────────────
// WebSocket Frame Opcodes (RFC 6455)
// ─────────────────────────────────────────────────────────────────────────────
enum class WSOpcode : uint8_t {
    CONTINUATION = 0x0,
    TEXT         = 0x1,
    BINARY       = 0x2,
    CLOSE        = 0x8,
    PING         = 0x9,
    PONG         = 0xA
};

// ─────────────────────────────────────────────────────────────────────────────
// WebSocket Connection State
// ─────────────────────────────────────────────────────────────────────────────
enum class WSState : uint8_t {
    DISCONNECTED = 0,
    CONNECTING   = 1,
    HANDSHAKING  = 2,
    CONNECTED    = 3,
    CLOSING      = 4,
    ERROR        = 5
};

// ─────────────────────────────────────────────────────────────────────────────
// WebSocket Connection
// ─────────────────────────────────────────────────────────────────────────────
class WebSocketConnection {
public:
    // Callback types
    using OnMessage = std::function<void(const char* data, size_t len, WSOpcode opcode)>;
    using OnStateChange = std::function<void(WSState old_state, WSState new_state)>;
    
    WebSocketConnection() noexcept
        : socket_(INVALID_SOCK)
        , ssl_ctx_(nullptr)
        , ssl_(nullptr)
        , state_(WSState::DISCONNECTED)
        , recv_buf_len_(0)
        , last_ping_ts_(0)
        , last_pong_ts_(0)
    {}
    
    ~WebSocketConnection() {
        disconnect();
        if (ssl_ctx_) {
            SSL_CTX_free(ssl_ctx_);
            ssl_ctx_ = nullptr;
        }
    }
    
    // Non-copyable
    WebSocketConnection(const WebSocketConnection&) = delete;
    WebSocketConnection& operator=(const WebSocketConnection&) = delete;
    
    // ═══════════════════════════════════════════════════════════════════════
    // CONNECTION MANAGEMENT
    // ═══════════════════════════════════════════════════════════════════════
    
    // Connect to WebSocket server
    // Returns true if connection initiated (may still be in handshake)
    [[nodiscard]] bool connect(
        const char* host,
        uint16_t port,
        const char* path
    ) noexcept {
        if (state_ != WSState::DISCONNECTED) {
            disconnect();
        }
        
        set_state(WSState::CONNECTING);
        
        // Store connection info for reconnect
        strncpy(host_, host, sizeof(host_) - 1);
        port_ = port;
        strncpy(path_, path, sizeof(path_) - 1);
        
        // Initialize SSL context if needed
        if (!ssl_ctx_) {
            ssl_ctx_ = SSL_CTX_new(TLS_client_method());
            if (!ssl_ctx_) {
                set_state(WSState::ERROR);
                return false;
            }
            SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_NONE, nullptr);
        }
        
        // Resolve hostname
        struct addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%u", port);
        
        struct addrinfo* result = nullptr;
        if (getaddrinfo(host, port_str, &hints, &result) != 0 || !result) {
            set_state(WSState::ERROR);
            return false;
        }
        
        // Create socket
        socket_ = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (socket_ == INVALID_SOCK) {
            freeaddrinfo(result);
            set_state(WSState::ERROR);
            return false;
        }
        
        // Set TCP_NODELAY for low latency
        int flag = 1;
        setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));
        
        // Connect
        if (::connect(socket_, result->ai_addr, result->ai_addrlen) != 0) {
            freeaddrinfo(result);
            SOCK_CLOSE(socket_);
            socket_ = INVALID_SOCK;
            set_state(WSState::ERROR);
            return false;
        }
        freeaddrinfo(result);
        
        // SSL handshake
        ssl_ = SSL_new(ssl_ctx_);
        if (!ssl_) {
            SOCK_CLOSE(socket_);
            socket_ = INVALID_SOCK;
            set_state(WSState::ERROR);
            return false;
        }
        
        SSL_set_fd(ssl_, static_cast<int>(socket_));
        SSL_set_tlsext_host_name(ssl_, host);
        
        if (SSL_connect(ssl_) != 1) {
            SSL_free(ssl_);
            ssl_ = nullptr;
            SOCK_CLOSE(socket_);
            socket_ = INVALID_SOCK;
            set_state(WSState::ERROR);
            return false;
        }
        
        // WebSocket handshake
        set_state(WSState::HANDSHAKING);
        if (!send_ws_handshake()) {
            disconnect();
            return false;
        }
        
        // Read handshake response
        if (!recv_ws_handshake()) {
            disconnect();
            return false;
        }
        
        // ═══════════════════════════════════════════════════════════════════
        // v4.9.22: SET NON-BLOCKING MODE AFTER HANDSHAKE
        // Critical fix: SSL_read() was blocking forever, poll() never returned
        // ═══════════════════════════════════════════════════════════════════
        #ifdef _WIN32
            unsigned long iMode = 1;  // 1 = non-blocking
            if (ioctlsocket(socket_, FIONBIO, &iMode) != 0) {
                printf("[WS] WARNING: Failed to set non-blocking mode (err=%d)\n", WSAGetLastError());
            } else {
                printf("[WS] ✓ Socket set to non-blocking mode\n");
            }
        #else
            int flags = fcntl(socket_, F_GETFL, 0);
            if (flags == -1 || fcntl(socket_, F_SETFL, flags | O_NONBLOCK) == -1) {
                printf("[WS] WARNING: Failed to set non-blocking mode (errno=%d)\n", errno);
            } else {
                printf("[WS] ✓ Socket set to non-blocking mode\n");
            }
        #endif
        
        set_state(WSState::CONNECTED);
        return true;
    }
    
    // Disconnect cleanly
    void disconnect() noexcept {
        if (ssl_) {
            SSL_shutdown(ssl_);
            SSL_free(ssl_);
            ssl_ = nullptr;
        }
        if (socket_ != INVALID_SOCK) {
            SOCK_CLOSE(socket_);
            socket_ = INVALID_SOCK;
        }
        set_state(WSState::DISCONNECTED);
        recv_buf_len_ = 0;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // DATA SENDING
    // ═══════════════════════════════════════════════════════════════════════
    
    // Send text message
    [[nodiscard]] bool send_text(const char* data, size_t len) noexcept {
        return send_frame(data, len, WSOpcode::TEXT);
    }
    
    // Send binary message
    [[nodiscard]] bool send_binary(const char* data, size_t len) noexcept {
        return send_frame(data, len, WSOpcode::BINARY);
    }
    
    // Send pong (response to ping)
    [[nodiscard]] bool send_pong(const char* data, size_t len) noexcept {
        return send_frame(data, len, WSOpcode::PONG);
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // DATA RECEIVING (call in loop)
    // v4.9.23: Proper select() gating - no CPU spin, no blocking
    // ═══════════════════════════════════════════════════════════════════════
    
    // Poll for incoming data, call message callback for each complete message
    // Returns number of messages processed, -1 on error, 0 if no data ready
    [[nodiscard]] int poll(OnMessage on_message, int timeout_ms = 1) noexcept {
        if (state_ != WSState::CONNECTED) return -1;
        if (socket_ == INVALID_SOCK) return -1;
        
        // ═══════════════════════════════════════════════════════════════════
        // v4.9.23: CHECK SOCKET READABILITY FIRST (prevents blocking AND spin)
        // ═══════════════════════════════════════════════════════════════════
        #ifdef _WIN32
            // Windows: use select()
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(socket_, &read_fds);
            
            struct timeval tv;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            
            int select_result = select(0, &read_fds, nullptr, nullptr, &tv);
            if (select_result == 0) {
                return 0;  // Timeout - no data available
            }
            if (select_result < 0) {
                int err = WSAGetLastError();
                if (err != WSAEINTR) {
                    printf("[WS] select() error: %d\n", err);
                    set_state(WSState::ERROR);
                    return -1;
                }
                return 0;
            }
        #else
            // Linux/Mac: use poll() (more efficient than select())
            struct pollfd pfd;
            pfd.fd = socket_;
            pfd.events = POLLIN;
            pfd.revents = 0;
            
            int poll_result = ::poll(&pfd, 1, timeout_ms);
            if (poll_result == 0) {
                return 0;  // Timeout - no data available
            }
            if (poll_result < 0) {
                if (errno != EINTR) {
                    printf("[WS] poll() error: %d\n", errno);
                    set_state(WSState::ERROR);
                    return -1;
                }
                return 0;
            }
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                printf("[WS] poll() socket error: revents=0x%x\n", pfd.revents);
                set_state(WSState::ERROR);
                return -1;
            }
        #endif
        
        // ═══════════════════════════════════════════════════════════════════
        // Socket is readable - drain SSL buffer
        // ═══════════════════════════════════════════════════════════════════
        int total_bytes = 0;
        int messages = 0;
        bool connection_closed = false;  // v4.9.31: Track closure but process data first
        
        // Drain all available data (SSL may have buffered multiple frames)
        while (true) {
            int bytes_read = SSL_read(ssl_, 
                recv_buf_ + recv_buf_len_, 
                sizeof(recv_buf_) - recv_buf_len_);
            
            if (bytes_read <= 0) {
                int err = SSL_get_error(ssl_, bytes_read);
                if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                    break;  // No more data in SSL buffer
                }
                // v4.9.31: Connection error - but process any data received first!
                printf("[WS] SSL_read error: %d (will process %zu buffered bytes first)\n", 
                       err, recv_buf_len_);
                connection_closed = true;
                break;  // Exit loop, but DON'T return yet - process buffered data
            }
            
            recv_buf_len_ += bytes_read;
            total_bytes += bytes_read;
            
            // Safety: don't overflow buffer
            if (recv_buf_len_ >= sizeof(recv_buf_) - 1024) {
                break;
            }
        }
        
        // v4.9.31: ALWAYS process any buffered data, even on connection close
        if (recv_buf_len_ > 0) {
            printf("[WS] Processing %zu buffered bytes before handling connection state\n", recv_buf_len_);
        }
        
        // Process complete frames
        size_t offset = 0;
        
        while (offset < recv_buf_len_) {
            size_t frame_len = 0;
            size_t payload_offset = 0;
            size_t payload_len = 0;
            WSOpcode opcode;
            
            if (!parse_frame(recv_buf_ + offset, recv_buf_len_ - offset,
                            frame_len, payload_offset, payload_len, opcode)) {
                break;  // Incomplete frame
            }
            
            // Handle frame based on opcode
            if (opcode == WSOpcode::TEXT || opcode == WSOpcode::BINARY) {
                if (on_message) {
                    on_message(recv_buf_ + offset + payload_offset, payload_len, opcode);
                }
                ++messages;
            } else if (opcode == WSOpcode::PING) {
                // Respond with pong (ignore return - best effort)
                (void)send_pong(recv_buf_ + offset + payload_offset, payload_len);
            } else if (opcode == WSOpcode::PONG) {
                last_pong_ts_ = get_monotonic_ns();
            } else if (opcode == WSOpcode::CLOSE) {
                // v4.9.31: Log close frame details
                const unsigned char* close_data = reinterpret_cast<const unsigned char*>(recv_buf_ + offset + payload_offset);
                if (payload_len >= 2) {
                    uint16_t close_code = (close_data[0] << 8) | close_data[1];
                    printf("[WS] CLOSE frame received: code=%u", close_code);
                    if (payload_len > 2) {
                        printf(" reason='%.*s'", static_cast<int>(payload_len - 2), close_data + 2);
                    }
                    printf("\n");
                } else {
                    printf("[WS] CLOSE frame received (no code)\n");
                }
                set_state(WSState::CLOSING);
                disconnect();
                return messages;
            }
            
            offset += frame_len;
        }
        
        // Shift remaining data to front of buffer
        if (offset > 0 && offset < recv_buf_len_) {
            memmove(recv_buf_, recv_buf_ + offset, recv_buf_len_ - offset);
            recv_buf_len_ -= offset;
        } else if (offset == recv_buf_len_) {
            recv_buf_len_ = 0;
        }
        
        // v4.9.31: NOW handle connection closure after processing all data
        if (connection_closed) {
            printf("[WS] Connection closed after processing %d messages\n", messages);
            set_state(WSState::ERROR);
            return -1;
        }
        
        return messages;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // STATE & ACCESSORS
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] WSState state() const noexcept { return state_; }
    [[nodiscard]] bool is_connected() const noexcept { return state_ == WSState::CONNECTED; }
    
    void set_on_state_change(OnStateChange cb) noexcept { on_state_change_ = cb; }
    
    // Reconnect using stored connection info
    [[nodiscard]] bool reconnect() noexcept {
        return connect(host_, port_, path_);
    }

private:
    // ═══════════════════════════════════════════════════════════════════════
    // INTERNAL HELPERS
    // ═══════════════════════════════════════════════════════════════════════
    
    void set_state(WSState new_state) noexcept {
        if (state_ != new_state) {
            WSState old = state_;
            state_ = new_state;
            if (on_state_change_) {
                on_state_change_(old, new_state);
            }
        }
    }
    
    // Send WebSocket upgrade request
    [[nodiscard]] bool send_ws_handshake() noexcept {
        // Generate random key (simplified - should be proper base64)
        char key[25];
        snprintf(key, sizeof(key), "dGhlIHNhbXBsZSBub25jZQ==");  // Fixed for simplicity
        
        char request[1024];
        int len = snprintf(request, sizeof(request),
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: %s\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n",
            path_, host_, key
        );
        
        return SSL_write(ssl_, request, len) == len;
    }
    
    // Receive and validate WebSocket handshake response
    [[nodiscard]] bool recv_ws_handshake() noexcept {
        char response[2048];
        int total = 0;
        
        // Read until we get complete headers
        while (total < (int)sizeof(response) - 1) {
            int n = SSL_read(ssl_, response + total, sizeof(response) - 1 - total);
            if (n <= 0) return false;
            total += n;
            response[total] = '\0';
            
            // Check for end of headers
            if (strstr(response, "\r\n\r\n")) break;
        }
        
        // Verify 101 Switching Protocols
        return strstr(response, "101") != nullptr;
    }
    
    // Send a WebSocket frame
    [[nodiscard]] bool send_frame(const char* data, size_t len, WSOpcode opcode) noexcept {
        if (!ssl_) return false;
        if (len > 65536) return false;  // Sanity limit
        
        // Use fixed buffer instead of VLA
        static thread_local unsigned char frame[65550];  // 14 header + 65536 max payload
        size_t frame_len = 0;
        
        // First byte: FIN + opcode
        frame[frame_len++] = 0x80 | static_cast<uint8_t>(opcode);
        
        // Second byte: MASK bit (client must mask) + payload length
        if (len < 126) {
            frame[frame_len++] = 0x80 | static_cast<uint8_t>(len);
        } else if (len < 65536) {
            frame[frame_len++] = 0x80 | 126;
            frame[frame_len++] = (len >> 8) & 0xFF;
            frame[frame_len++] = len & 0xFF;
        } else {
            frame[frame_len++] = 0x80 | 127;
            for (int i = 7; i >= 0; --i) {
                frame[frame_len++] = (len >> (i * 8)) & 0xFF;
            }
        }
        
        // Masking key (4 bytes) - v4.9.31: Use random key per RFC 6455
        // Simple fast random using XORshift with timestamp seed
        static thread_local uint32_t mask_state = static_cast<uint32_t>(get_monotonic_ns() ^ 0xDEADBEEF);
        mask_state ^= mask_state << 13;
        mask_state ^= mask_state >> 17;
        mask_state ^= mask_state << 5;
        uint32_t mask_key = mask_state;
        memcpy(frame + frame_len, &mask_key, 4);
        frame_len += 4;
        
        // Masked payload
        const unsigned char* mask = frame + frame_len - 4;
        for (size_t i = 0; i < len; ++i) {
            frame[frame_len++] = data[i] ^ mask[i % 4];
        }
        
        return SSL_write(ssl_, frame, frame_len) == (int)frame_len;
    }
    
    // Parse a WebSocket frame from buffer
    // Returns false if incomplete frame
    [[nodiscard]] bool parse_frame(
        const char* buf, 
        size_t buf_len,
        size_t& frame_len,
        size_t& payload_offset,
        size_t& payload_len,
        WSOpcode& opcode
    ) noexcept {
        if (buf_len < 2) return false;
        
        const unsigned char* data = reinterpret_cast<const unsigned char*>(buf);
        
        // First byte: FIN + opcode
        bool fin = (data[0] & 0x80) != 0;
        opcode = static_cast<WSOpcode>(data[0] & 0x0F);
        (void)fin;  // We don't handle fragmentation for now
        
        // Second byte: MASK + payload length
        bool masked = (data[1] & 0x80) != 0;
        uint64_t len = data[1] & 0x7F;
        
        size_t header_len = 2;
        
        if (len == 126) {
            if (buf_len < 4) return false;
            len = (data[2] << 8) | data[3];
            header_len = 4;
        } else if (len == 127) {
            if (buf_len < 10) return false;
            len = 0;
            for (int i = 0; i < 8; ++i) {
                len = (len << 8) | data[2 + i];
            }
            header_len = 10;
        }
        
        if (masked) {
            header_len += 4;  // Mask key
        }
        
        if (buf_len < header_len + len) return false;
        
        // Unmask payload if needed (server shouldn't send masked frames, but handle it)
        if (masked) {
            const unsigned char* mask = data + header_len - 4;
            unsigned char* payload = const_cast<unsigned char*>(data + header_len);
            for (size_t i = 0; i < len; ++i) {
                payload[i] ^= mask[i % 4];
            }
        }
        
        frame_len = header_len + len;
        payload_offset = header_len;
        payload_len = len;
        
        return true;
    }
    
    // Get monotonic time in nanoseconds
    static uint64_t get_monotonic_ns() noexcept {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(
            steady_clock::now().time_since_epoch()
        ).count();
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // MEMBER DATA
    // ═══════════════════════════════════════════════════════════════════════
    
    socket_t    socket_;
    SSL_CTX*    ssl_ctx_;
    SSL*        ssl_;
    WSState     state_;
    
    // Connection info (for reconnect)
    char        host_[256];
    uint16_t    port_;
    char        path_[512];
    
    // Receive buffer
    char        recv_buf_[65536];
    size_t      recv_buf_len_;
    
    // Keepalive tracking
    uint64_t    last_ping_ts_;
    uint64_t    last_pong_ts_;
    
    // Callbacks
    OnStateChange on_state_change_;
};

} // namespace Binance
} // namespace Chimera
