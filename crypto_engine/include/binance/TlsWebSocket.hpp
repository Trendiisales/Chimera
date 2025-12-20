#pragma once
#include <string>
#include <functional>
#include <cstdint>

namespace binance {

/*
 Minimal TLS + WebSocket client (RFC6455, client-correct).
*/
class TlsWebSocket {
public:
    using OnMessage = std::function<void(const std::string&)>;

    TlsWebSocket(const std::string& host, int port, const std::string& path);
    ~TlsWebSocket();

    bool connect();
    void set_on_message(OnMessage cb);

    void send_text(const std::string& msg);
    void send_ping();
    void run();
    void stop();

private:
    std::string host;
    int port;
    std::string path;

    int sock{-1};
    void* ssl_ctx{nullptr};
    void* ssl{nullptr};
    bool running{false};

    OnMessage on_message;

    bool tcp_connect();
    bool tls_handshake();
    bool ws_handshake();

    void send_frame(uint8_t opcode, const std::string& payload);
};

}
