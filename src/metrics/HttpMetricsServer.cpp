#include "metrics/HttpMetricsServer.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

HttpMetricsServer::HttpMetricsServer(int port)
: port_(port), server_fd_(-1), intents_(0), running_(false) {}

HttpMetricsServer::~HttpMetricsServer() { stop(); }

void HttpMetricsServer::inc_intents() {
    intents_.fetch_add(1, std::memory_order_relaxed);
}

void HttpMetricsServer::start() {
    running_.store(true);
    worker_ = std::thread([this]() {
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);
        bind(server_fd_, (sockaddr*)&addr, sizeof(addr));
        listen(server_fd_, 4);

        while (running_.load()) {
            int c = accept(server_fd_, nullptr, nullptr);
            if (c < 0) continue;
            const char* hdr =
                "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n";
            write(c, hdr, strlen(hdr));
            char buf[128];
            int n = snprintf(buf, sizeof(buf),
                             "chimera_intents_total %llu\n",
                             intents_.load());
            write(c, buf, n);
            close(c);
        }
    });
}

void HttpMetricsServer::stop() {
    running_.store(false);
    if (server_fd_ >= 0) close(server_fd_);
    if (worker_.joinable()) worker_.join();
}
