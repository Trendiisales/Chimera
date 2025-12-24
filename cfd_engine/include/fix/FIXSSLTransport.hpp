#pragma once
// =============================================================================
// FIXSSLTransport.hpp - SSL Transport for cTrader FIX 4.4
// =============================================================================
// CHIMERA HFT - Secure FIX Transport Layer
// Target: Linux (WSL2) on Windows VPS
// Protocol: TLS 1.2/1.3 over TCP
// =============================================================================
// DEBUG BUILD - EXTENSIVE LOGGING TO CATCH CRASH
// =============================================================================

#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <cstdio>

// Platform-specific includes
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "crypt32.lib")
    typedef int socklen_t;
    #define SOCKET_ERROR_CODE WSAGetLastError()
    #define CLOSE_SOCKET(s) closesocket(s)
    #define INVALID_SOCKET_VAL INVALID_SOCKET
    #define GET_THREAD_ID() GetCurrentThreadId()
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    #include <fcntl.h>
    #include <errno.h>
    #include <pthread.h>
    #define SOCKET int
    #define SOCKET_ERROR_CODE errno
    #define CLOSE_SOCKET(s) ::close(s)
    #define INVALID_SOCKET_VAL (-1)
    #define GET_THREAD_ID() pthread_self()
#endif

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/x509.h>

namespace Chimera {

// =============================================================================
// DEBUG LOGGING MACROS - FLUSH IMMEDIATELY TO CATCH CRASH LOCATION
// =============================================================================
#define FIX_DEBUG(msg) do { \
    fprintf(stderr, "[FIX-DBG][%s:%d][t=%lu] %s\n", \
        __func__, __LINE__, (unsigned long)GET_THREAD_ID(), msg); \
    fflush(stderr); \
} while(0)

#define FIX_DEBUG_FMT(fmt, ...) do { \
    fprintf(stderr, "[FIX-DBG][%s:%d][t=%lu] " fmt "\n", \
        __func__, __LINE__, (unsigned long)GET_THREAD_ID(), __VA_ARGS__); \
    fflush(stderr); \
} while(0)

// =============================================================================
// FIX TRANSPORT CALLBACK TYPES
// =============================================================================
using FIXRxCallback = std::function<void(const std::string&)>;
using FIXStateCallback = std::function<void(bool connected)>;

// =============================================================================
// FIX SSL TRANSPORT CLASS
// =============================================================================
class FIXSSLTransport {
public:
    static constexpr char SOH = '\x01';
    static constexpr size_t RECV_BUFFER_SIZE = 65536;
    static constexpr size_t MAX_MSG_SIZE = 8192;
    
    FIXSSLTransport() 
        : instanceId_(nextInstanceId_++)
        , sock_(INVALID_SOCKET_VAL)
        , port_(0)
        , sslCtx_(nullptr)
        , ssl_(nullptr)
        , running_(false)
        , connected_(false)
        , bytesSent_(0)
        , bytesRecv_(0)
        , msgsSent_(0)
        , msgsRecv_(0)
    {
        FIX_DEBUG_FMT("CONSTRUCTOR instance=%d this=%p", instanceId_, (void*)this);
        FIX_DEBUG_FMT("  rxThread_.joinable()=%d txThread_.joinable()=%d", 
            rxThread_.joinable() ? 1 : 0, txThread_.joinable() ? 1 : 0);
        initSSL();
        FIX_DEBUG_FMT("CONSTRUCTOR DONE instance=%d sslCtx_=%p", instanceId_, (void*)sslCtx_);
    }
    
    ~FIXSSLTransport() {
        FIX_DEBUG_FMT("DESTRUCTOR START instance=%d", instanceId_);
        FIX_DEBUG_FMT("  rxThread_.joinable()=%d txThread_.joinable()=%d", 
            rxThread_.joinable() ? 1 : 0, txThread_.joinable() ? 1 : 0);
        disconnect();
        cleanupSSL();
        FIX_DEBUG_FMT("DESTRUCTOR DONE instance=%d", instanceId_);
    }
    
    // =========================================================================
    // CONNECTION MANAGEMENT
    // =========================================================================
    
    bool connect(const std::string& host, int port) {
        FIX_DEBUG_FMT(">>> connect() START instance=%d host=%s port=%d", instanceId_, host.c_str(), port);
        FIX_DEBUG_FMT("  rxThread_.joinable()=%d txThread_.joinable()=%d BEFORE lock", 
            rxThread_.joinable() ? 1 : 0, txThread_.joinable() ? 1 : 0);
        
        std::lock_guard<std::mutex> lock(connMtx_);
        FIX_DEBUG("  connMtx_ LOCKED");
        
        if (connected_.load()) {
            FIX_DEBUG("  ERROR: Already connected, returning false");
            return false;
        }
        
        host_ = host;
        port_ = port;
        
        // Create socket
        FIX_DEBUG("  Creating socket...");
        sock_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock_ == INVALID_SOCKET_VAL) {
            FIX_DEBUG_FMT("  ERROR: Socket creation failed: %d", SOCKET_ERROR_CODE);
            return false;
        }
        FIX_DEBUG_FMT("  Socket created: fd=%d", sock_);
        
        // Resolve hostname
        FIX_DEBUG("  Resolving hostname...");
        struct addrinfo hints{}, *result = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        
        std::string portStr = std::to_string(port);
        int rv = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result);
        if (rv != 0 || !result) {
            FIX_DEBUG_FMT("  ERROR: DNS failed: %d", rv);
            CLOSE_SOCKET(sock_);
            sock_ = INVALID_SOCKET_VAL;
            return false;
        }
        FIX_DEBUG("  DNS OK");
        
        // Socket options
        int flag = 1;
        setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY, (const char*)&flag, sizeof(flag));
        
        // Set non-blocking for connect with timeout
#ifdef _WIN32
        u_long nonblock = 1;
        ioctlsocket(sock_, FIONBIO, &nonblock);
#else
        int flags = fcntl(sock_, F_GETFL, 0);
        fcntl(sock_, F_SETFL, flags | O_NONBLOCK);
#endif
        
        // TCP connect (non-blocking)
        FIX_DEBUG("  TCP connect (non-blocking)...");
        int connectResult = ::connect(sock_, result->ai_addr, result->ai_addrlen);
        freeaddrinfo(result);
        
        if (connectResult != 0) {
#ifdef _WIN32
            if (WSAGetLastError() != WSAEWOULDBLOCK) {
#else
            if (errno != EINPROGRESS) {
#endif
                FIX_DEBUG_FMT("  ERROR: TCP connect failed immediately: %d", SOCKET_ERROR_CODE);
                CLOSE_SOCKET(sock_);
                sock_ = INVALID_SOCKET_VAL;
                return false;
            }
            
            // Wait for connect with 10 second timeout
            fd_set writefds;
            FD_ZERO(&writefds);
            FD_SET(sock_, &writefds);
            
            struct timeval tv;
            tv.tv_sec = 10;  // 10 second connect timeout
            tv.tv_usec = 0;
            
            FIX_DEBUG("  Waiting for connect (10s timeout)...");
            int selectResult = select(sock_ + 1, NULL, &writefds, NULL, &tv);
            
            if (selectResult <= 0) {
                FIX_DEBUG_FMT("  ERROR: TCP connect timeout or error: %d", selectResult);
                CLOSE_SOCKET(sock_);
                sock_ = INVALID_SOCKET_VAL;
                return false;
            }
            
            // Check if connect succeeded
            int error = 0;
            socklen_t len = sizeof(error);
            getsockopt(sock_, SOL_SOCKET, SO_ERROR, (char*)&error, &len);
            if (error != 0) {
                FIX_DEBUG_FMT("  ERROR: TCP connect failed: %d", error);
                CLOSE_SOCKET(sock_);
                sock_ = INVALID_SOCKET_VAL;
                return false;
            }
        }
        
        // Set back to blocking for SSL
#ifdef _WIN32
        nonblock = 0;
        ioctlsocket(sock_, FIONBIO, &nonblock);
#else
        fcntl(sock_, F_SETFL, flags);  // Remove O_NONBLOCK
#endif
        
        // Set receive timeout for SSL operations
        struct timeval tv;
        tv.tv_sec = 30;
        tv.tv_usec = 0;
        setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        
        FIX_DEBUG("  TCP connected OK");
        
        // SSL handshake
        FIX_DEBUG("  SSL handshake...");
        if (!sslHandshake()) {
            FIX_DEBUG("  ERROR: SSL handshake failed");
            CLOSE_SOCKET(sock_);
            sock_ = INVALID_SOCKET_VAL;
            return false;
        }
        FIX_DEBUG("  SSL handshake OK");
        
        // Set state BEFORE creating threads
        FIX_DEBUG("  Setting running_=true connected_=true");
        running_.store(true);
        connected_.store(true);
        
        // CHECK for joinable threads - this would indicate a bug
        FIX_DEBUG_FMT("  THREAD CHECK: rxThread_.joinable()=%d txThread_.joinable()=%d",
            rxThread_.joinable() ? 1 : 0, txThread_.joinable() ? 1 : 0);
        
        if (rxThread_.joinable()) {
            FIX_DEBUG("  !!! BUG: rxThread_ still joinable - attempting recovery");
            running_.store(false);
            rxThread_.join();
            running_.store(true);
            FIX_DEBUG("  !!! Recovered: rxThread_ joined");
        }
        if (txThread_.joinable()) {
            FIX_DEBUG("  !!! BUG: txThread_ still joinable - attempting recovery");
            running_.store(false);
            txCv_.notify_all();
            txThread_.join();
            running_.store(true);
            FIX_DEBUG("  !!! Recovered: txThread_ joined");
        }
        
        // CREATE RX THREAD
        FIX_DEBUG("  >>> Creating RX thread...");
        try {
            rxThread_ = std::thread(&FIXSSLTransport::rxLoop, this);
            FIX_DEBUG_FMT("  RX thread CREATED handle=%lu", (unsigned long)rxThread_.native_handle());
        } catch (const std::exception& e) {
            FIX_DEBUG_FMT("  !!! EXCEPTION creating RX thread: %s", e.what());
            running_.store(false);
            connected_.store(false);
            CLOSE_SOCKET(sock_);
            sock_ = INVALID_SOCKET_VAL;
            return false;
        } catch (...) {
            FIX_DEBUG("  !!! UNKNOWN EXCEPTION creating RX thread");
            running_.store(false);
            connected_.store(false);
            CLOSE_SOCKET(sock_);
            sock_ = INVALID_SOCKET_VAL;
            return false;
        }
        
        // CREATE TX THREAD
        FIX_DEBUG("  >>> Creating TX thread...");
        try {
            txThread_ = std::thread(&FIXSSLTransport::txLoop, this);
            FIX_DEBUG_FMT("  TX thread CREATED handle=%lu", (unsigned long)txThread_.native_handle());
        } catch (const std::exception& e) {
            FIX_DEBUG_FMT("  !!! EXCEPTION creating TX thread: %s", e.what());
            running_.store(false);
            if (rxThread_.joinable()) rxThread_.join();
            connected_.store(false);
            CLOSE_SOCKET(sock_);
            sock_ = INVALID_SOCKET_VAL;
            return false;
        } catch (...) {
            FIX_DEBUG("  !!! UNKNOWN EXCEPTION creating TX thread");
            running_.store(false);
            if (rxThread_.joinable()) rxThread_.join();
            connected_.store(false);
            CLOSE_SOCKET(sock_);
            sock_ = INVALID_SOCKET_VAL;
            return false;
        }
        
        FIX_DEBUG("  Both threads created successfully");
        
        // NOTE: We no longer need a fixed delay here because FIXSession::start()
        // now calls waitForRxReady() which properly synchronizes with the RX thread.
        // The RX thread signals rxReady_ when it's actually blocking on SSL_read.
        
        // State callback
        if (stateCallback_) {
            FIX_DEBUG("  Calling stateCallback_(true)...");
            try {
                stateCallback_(true);
                FIX_DEBUG("  stateCallback_ returned OK");
            } catch (const std::exception& e) {
                FIX_DEBUG_FMT("  !!! stateCallback_ threw: %s", e.what());
            } catch (...) {
                FIX_DEBUG("  !!! stateCallback_ threw unknown exception");
            }
        }
        
        FIX_DEBUG_FMT(">>> connect() COMPLETE instance=%d SUCCESS", instanceId_);
        return true;
    }
    
    void disconnect() {
        FIX_DEBUG_FMT(">>> disconnect() START instance=%d", instanceId_);
        FIX_DEBUG_FMT("  running_=%d connected_=%d sock_=%d", 
            running_.load() ? 1 : 0, connected_.load() ? 1 : 0, sock_);
        FIX_DEBUG_FMT("  rxThread_.joinable()=%d txThread_.joinable()=%d", 
            rxThread_.joinable() ? 1 : 0, txThread_.joinable() ? 1 : 0);
        
        std::lock_guard<std::mutex> connLock(connMtx_);
        FIX_DEBUG("  connMtx_ LOCKED");
        
        if (!running_.load() && sock_ == INVALID_SOCKET_VAL) {
            FIX_DEBUG("  Already disconnected, returning");
            return;
        }
        
        FIX_DEBUG("  Setting running_=false connected_=false rxReady_=false");
        running_.store(false);
        connected_.store(false);
        rxReady_.store(false, std::memory_order_release);  // Reset RX readiness
        
        FIX_DEBUG("  Notifying txCv_");
        txCv_.notify_all();
        
        // =================================================================
        // AGGRESSIVE SHUTDOWN SEQUENCE:
        // 1. Close read side of socket FIRST to unblock SSL_read immediately
        // 2. Then do SSL_shutdown
        // 3. Then close full socket
        // This ensures SSL_read returns quickly even on blocking sockets
        // =================================================================
        
        // STEP 1: Shutdown read side FIRST to unblock SSL_read
        if (sock_ != INVALID_SOCKET_VAL) {
            FIX_DEBUG_FMT("  shutdown(SHUT_RD) on fd=%d to unblock SSL_read", sock_);
            #ifdef _WIN32
            shutdown(sock_, SD_RECEIVE);
            #else
            shutdown(sock_, SHUT_RD);
            #endif
        }
        
        // STEP 2: SSL_shutdown to send close_notify
        {
            std::lock_guard<std::mutex> sslLock(sslMtx_);
            if (ssl_) {
                FIX_DEBUG("  SSL_shutdown()");
                // Non-blocking shutdown - don't wait for peer response
                SSL_set_shutdown(ssl_, SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN);
                SSL_shutdown(ssl_);
            }
        }
        
        // STEP 3: Close full socket
        if (sock_ != INVALID_SOCKET_VAL) {
            FIX_DEBUG_FMT("  Closing socket fd=%d", sock_);
            #ifdef _WIN32
            shutdown(sock_, SD_BOTH);
            #else
            shutdown(sock_, SHUT_RDWR);
            #endif
            CLOSE_SOCKET(sock_);
            sock_ = INVALID_SOCKET_VAL;
            FIX_DEBUG("  Socket closed");
        }
        
        // Join threads with timeout protection
        if (rxThread_.joinable()) {
            FIX_DEBUG("  Joining RX thread...");
            rxThread_.join();
            FIX_DEBUG("  RX thread joined");
        }
        
        if (txThread_.joinable()) {
            FIX_DEBUG("  Joining TX thread...");
            txThread_.join();
            FIX_DEBUG("  TX thread joined");
        }
        
        // Cleanup SSL after threads are done
        {
            std::lock_guard<std::mutex> sslLock(sslMtx_);
            if (ssl_) {
                FIX_DEBUG("  SSL_free()");
                SSL_free(ssl_);
                ssl_ = nullptr;
            }
        }
        
        // Clear queue
        {
            std::lock_guard<std::mutex> txLock(txMtx_);
            while (!txQueue_.empty()) txQueue_.pop();
        }
        
        FIX_DEBUG_FMT(">>> disconnect() COMPLETE instance=%d", instanceId_);
    }
    
    // =========================================================================
    // SEND/RECEIVE
    // =========================================================================
    
    bool sendRaw(const std::string& msg) {
        if (!connected_.load()) return false;
        {
            std::lock_guard<std::mutex> lock(txMtx_);
            txQueue_.push(msg);
        }
        txCv_.notify_one();
        return true;
    }
    
    void setRxCallback(FIXRxCallback cb) { rxCallback_ = std::move(cb); }
    void setStateCallback(FIXStateCallback cb) { stateCallback_ = std::move(cb); }
    
    bool isConnected() const { return connected_.load(); }
    bool isRxReady() const { return rxReady_.load(); }
    uint64_t getBytesSent() const { return bytesSent_.load(); }
    uint64_t getBytesRecv() const { return bytesRecv_.load(); }
    uint64_t getMsgsSent() const { return msgsSent_.load(); }
    uint64_t getMsgsRecv() const { return msgsRecv_.load(); }
    
    // =========================================================================
    // RX READINESS - MUST BE CALLED BEFORE SENDING LOGON
    // This ensures RX thread is blocking on SSL_read before any data is sent
    // =========================================================================
    bool waitForRxReady(int timeoutMs = 5000) {
        FIX_DEBUG_FMT("waitForRxReady() START timeout=%dms", timeoutMs);
        
        std::unique_lock<std::mutex> lock(rxReadyMtx_);
        bool ready = rxReadyCv_.wait_for(lock, 
            std::chrono::milliseconds(timeoutMs),
            [this] { return rxReady_.load(std::memory_order_acquire); });
        
        if (ready) {
            FIX_DEBUG("waitForRxReady() RX THREAD IS READY - safe to send LOGON");
        } else {
            FIX_DEBUG("waitForRxReady() TIMEOUT - RX thread not ready!");
        }
        return ready;
    }
    
private:
    // =========================================================================
    // SSL INITIALIZATION
    // =========================================================================
    
    void initSSL() {
        FIX_DEBUG_FMT("initSSL() instance=%d", instanceId_);
        
        static std::once_flag initFlag;
        static std::atomic<bool> initDone{false};
        
        std::call_once(initFlag, []() {
            FIX_DEBUG("  Global SSL init...");
            #if OPENSSL_VERSION_NUMBER >= 0x10100000L
                OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);
                FIX_DEBUG("  OpenSSL 1.1+ init done");
            #else
                SSL_library_init();
                SSL_load_error_strings();
                OpenSSL_add_all_algorithms();
                FIX_DEBUG("  OpenSSL 1.0 init done");
            #endif
            initDone.store(true);
        });
        
        while (!initDone.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
        ERR_clear_error();
        
        FIX_DEBUG("  SSL_CTX_new...");
        const SSL_METHOD* method = TLS_client_method();
        sslCtx_ = SSL_CTX_new(method);
        if (!sslCtx_) {
            unsigned long err = ERR_get_error();
            char errbuf[256];
            ERR_error_string_n(err, errbuf, sizeof(errbuf));
            FIX_DEBUG_FMT("  ERROR: SSL_CTX_new failed: %s", errbuf);
            return;
        }
        
        SSL_CTX_set_min_proto_version(sslCtx_, TLS1_2_VERSION);
        SSL_CTX_set_verify(sslCtx_, SSL_VERIFY_NONE, nullptr);
        
        FIX_DEBUG_FMT("initSSL() DONE sslCtx_=%p", (void*)sslCtx_);
    }
    
    bool sslHandshake() {
        FIX_DEBUG_FMT("sslHandshake() instance=%d", instanceId_);
        
        std::lock_guard<std::mutex> lock(sslMtx_);
        ERR_clear_error();
        
        FIX_DEBUG_FMT("  SSL_new(ctx=%p)...", (void*)sslCtx_);
        ssl_ = SSL_new(sslCtx_);
        if (!ssl_) {
            unsigned long err = ERR_get_error();
            char errbuf[256];
            ERR_error_string_n(err, errbuf, sizeof(errbuf));
            FIX_DEBUG_FMT("  ERROR: SSL_new failed: %s", errbuf);
            return false;
        }
        FIX_DEBUG_FMT("  ssl_=%p", (void*)ssl_);
        
        SSL_set_fd(ssl_, sock_);
        SSL_set_tlsext_host_name(ssl_, host_.c_str());
        
        ERR_clear_error();
        FIX_DEBUG("  SSL_connect...");
        int ret = SSL_connect(ssl_);
        if (ret != 1) {
            int err = SSL_get_error(ssl_, ret);
            char errbuf[256];
            ERR_error_string_n(ERR_get_error(), errbuf, sizeof(errbuf));
            FIX_DEBUG_FMT("  ERROR: SSL_connect: ret=%d err=%d %s", ret, err, errbuf);
            SSL_free(ssl_);
            ssl_ = nullptr;
            return false;
        }
        
        FIX_DEBUG_FMT("  SSL connected: cipher=%s", SSL_get_cipher(ssl_));
        return true;
    }
    
    void cleanupSSL() {
        FIX_DEBUG_FMT("cleanupSSL() sslCtx_=%p", (void*)sslCtx_);
        if (sslCtx_) {
            SSL_CTX_free(sslCtx_);
            sslCtx_ = nullptr;
        }
    }
    
    // =========================================================================
    // RX THREAD
    // =========================================================================
    
    void rxLoop() {
        FIX_DEBUG_FMT(">>> rxLoop() START instance=%d", instanceId_);
        
        try {
            FIX_DEBUG("  Allocating buffer...");
            char buffer[RECV_BUFFER_SIZE];
            std::string rxBuffer;
            rxBuffer.reserve(MAX_MSG_SIZE * 4);
            FIX_DEBUG("  Buffer allocated OK");
            
            // =====================================================================
            // CRITICAL: Signal that RX is ready BEFORE entering read loop
            // This allows waitForRxReady() to return, enabling LOGON to be sent
            // The server's immediate ACK will be caught because we're now listening
            // =====================================================================
            {
                std::lock_guard<std::mutex> lock(rxReadyMtx_);
                rxReady_.store(true, std::memory_order_release);
                FIX_DEBUG("  RX READY - signaling waiters");
            }
            rxReadyCv_.notify_all();
            
            FIX_DEBUG("  Entering read loop...");
            int loopCount = 0;
            
            while (running_.load()) {
                int n;
                int ssl_err = 0;
                
                // NOTE: We do NOT hold sslMtx_ during SSL_read because it blocks!
                // OpenSSL 1.1+ is thread-safe for concurrent read/write on same SSL*
                // as long as we're not doing partial reads/writes simultaneously
                // The mutex is only needed for SSL lifecycle (connect/disconnect)
                
                SSL* localSsl = nullptr;
                {
                    std::lock_guard<std::mutex> lock(sslMtx_);
                    localSsl = ssl_;
                }
                
                if (!localSsl) {
                    FIX_DEBUG("  ssl_ is NULL, breaking");
                    break;
                }
                
                // SSL_read without holding mutex - allows TX thread to write
                n = SSL_read(localSsl, buffer, sizeof(buffer));
                if (n <= 0) {
                    std::lock_guard<std::mutex> lock(sslMtx_);
                    if (ssl_) {
                        ssl_err = SSL_get_error(ssl_, n);
                    }
                }
                
                if (n > 0) {
                    // DEBUG: Print received bytes
                    fprintf(stderr, "[FIX-DBG][RX-BYTES] Received %d bytes: ", n);
                    for (int i = 0; i < std::min(n, 200); i++) {
                        char c = buffer[i];
                        if (c == '\x01') fprintf(stderr, "|");
                        else if (c >= 32 && c < 127) fprintf(stderr, "%c", c);
                        else fprintf(stderr, "\\x%02x", (unsigned char)c);
                    }
                    fprintf(stderr, "\n");
                    fflush(stderr);
                    
                    bytesRecv_.fetch_add(n);
                    rxBuffer.append(buffer, n);
                    processBuffer(rxBuffer);
                    loopCount++;
                    if (loopCount % 100 == 0) {
                        FIX_DEBUG_FMT("  rxLoop iteration %d, bytes=%lu", loopCount, bytesRecv_.load());
                    }
                } else if (n == 0) {
                    FIX_DEBUG("  Peer closed connection");
                    break;
                } else {
                    if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
                        continue;
                    }
                    if (ssl_err == SSL_ERROR_SYSCALL) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            continue;
                        }
                        if (errno == 0 || errno == ECONNRESET || errno == EPIPE) {
                            FIX_DEBUG_FMT("  Socket closed (errno=%d)", errno);
                            break;
                        }
                    }
                    FIX_DEBUG_FMT("  SSL_read error: ssl_err=%d errno=%d", ssl_err, errno);
                    break;
                }
            }
            
            FIX_DEBUG("  Read loop ended");
            
            if (running_.load()) {
                FIX_DEBUG("  Unexpected disconnect");
                connected_.store(false);
                if (stateCallback_) {
                    try { stateCallback_(false); } catch (...) {}
                }
            }
            
        } catch (const std::exception& e) {
            FIX_DEBUG_FMT("!!! rxLoop EXCEPTION: %s", e.what());
            running_.store(false);
            connected_.store(false);
        } catch (...) {
            FIX_DEBUG("!!! rxLoop UNKNOWN EXCEPTION");
            running_.store(false);
            connected_.store(false);
        }
        
        FIX_DEBUG_FMT(">>> rxLoop() COMPLETE instance=%d", instanceId_);
    }
    
    void processBuffer(std::string& buffer) {
        size_t pos = 0;
        while (pos < buffer.length()) {
            size_t checksum_pos = buffer.find("10=", pos);
            if (checksum_pos == std::string::npos) break;
            
            size_t end_pos = buffer.find(SOH, checksum_pos);
            if (end_pos == std::string::npos) break;
            
            std::string msg = buffer.substr(pos, end_pos - pos + 1);
            msgsRecv_.fetch_add(1);
            
            if (rxCallback_) {
                try {
                    rxCallback_(msg);
                } catch (const std::exception& e) {
                    FIX_DEBUG_FMT("!!! rxCallback exception: %s", e.what());
                } catch (...) {
                    FIX_DEBUG("!!! rxCallback unknown exception");
                }
            }
            
            pos = end_pos + 1;
        }
        
        if (pos > 0) {
            buffer.erase(0, pos);
        }
    }
    
    // =========================================================================
    // TX THREAD
    // =========================================================================
    
    void txLoop() {
        FIX_DEBUG_FMT(">>> txLoop() START instance=%d", instanceId_);
        
        try {
            while (running_.load()) {
                std::string msg;
                {
                    std::unique_lock<std::mutex> lock(txMtx_);
                    txCv_.wait(lock, [this]() {
                        return !txQueue_.empty() || !running_.load();
                    });
                    
                    if (!running_.load() && txQueue_.empty()) break;
                    if (txQueue_.empty()) continue;
                    
                    msg = std::move(txQueue_.front());
                    txQueue_.pop();
                    FIX_DEBUG_FMT("  TX dequeued %zu bytes", msg.length());
                }
                
                // Get SSL pointer without holding mutex during write
                SSL* localSsl = nullptr;
                {
                    std::lock_guard<std::mutex> lock(sslMtx_);
                    localSsl = ssl_;
                }
                
                if (!localSsl) {
                    FIX_DEBUG("  ssl_ is NULL, breaking");
                    break;
                }
                
                // DEBUG: Print actual bytes being sent (first 200)
                fprintf(stderr, "[FIX-DBG][TX-BYTES] Sending %zu bytes: ", msg.length());
                for (size_t i = 0; i < std::min(msg.length(), (size_t)200); i++) {
                    char c = msg[i];
                    if (c == '\x01') fprintf(stderr, "|");
                    else if (c >= 32 && c < 127) fprintf(stderr, "%c", c);
                    else fprintf(stderr, "\\x%02x", (unsigned char)c);
                }
                fprintf(stderr, "\n");
                fflush(stderr);
                
                // SSL_write without holding mutex - allows RX thread to read
                size_t total_sent = 0;
                while (total_sent < msg.length() && running_.load()) {
                    FIX_DEBUG_FMT("  SSL_write attempt: offset=%zu remaining=%zu", 
                        total_sent, msg.length() - total_sent);
                    int n = SSL_write(localSsl, msg.data() + total_sent, msg.length() - total_sent);
                    FIX_DEBUG_FMT("  SSL_write returned: %d", n);
                    if (n <= 0) {
                        int err = SSL_get_error(localSsl, n);
                        FIX_DEBUG_FMT("  SSL_write error: n=%d err=%d errno=%d", n, err, errno);
                        if (err == SSL_ERROR_WANT_WRITE) continue;
                        break;
                    }
                    total_sent += n;
                    FIX_DEBUG_FMT("  SSL_write sent %d bytes, total=%zu", n, total_sent);
                }
                
                if (total_sent == msg.length()) {
                    bytesSent_.fetch_add(total_sent);
                    msgsSent_.fetch_add(1);
                    FIX_DEBUG_FMT("  TX COMPLETE: sent=%zu bytes OK", total_sent);
                } else {
                    FIX_DEBUG_FMT("  TX PARTIAL sent=%zu expected=%zu", total_sent, msg.length());
                }
            }
            
        } catch (const std::exception& e) {
            FIX_DEBUG_FMT("!!! txLoop EXCEPTION: %s", e.what());
            running_.store(false);
        } catch (...) {
            FIX_DEBUG("!!! txLoop UNKNOWN EXCEPTION");
            running_.store(false);
        }
        
        FIX_DEBUG_FMT(">>> txLoop() COMPLETE instance=%d", instanceId_);
    }
    
private:
    int instanceId_;
    static inline std::atomic<int> nextInstanceId_{0};
    
    SOCKET sock_;
    std::string host_;
    int port_;
    
    SSL_CTX* sslCtx_;
    SSL* ssl_;
    std::mutex sslMtx_;
    
    std::atomic<bool> running_;
    std::atomic<bool> connected_;
    std::mutex connMtx_;
    
    // RX READINESS SIGNALING - fixes race condition
    std::atomic<bool> rxReady_{false};
    std::mutex rxReadyMtx_;
    std::condition_variable rxReadyCv_;
    
    std::thread rxThread_;
    std::thread txThread_;
    
    std::queue<std::string> txQueue_;
    std::mutex txMtx_;
    std::condition_variable txCv_;
    
    FIXRxCallback rxCallback_;
    FIXStateCallback stateCallback_;
    
    std::atomic<uint64_t> bytesSent_;
    std::atomic<uint64_t> bytesRecv_;
    std::atomic<uint64_t> msgsSent_;
    std::atomic<uint64_t> msgsRecv_;
};

} // namespace Chimera
