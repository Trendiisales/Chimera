#include "gui/WsServer.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <set>
#include <vector>

static std::set<int> clients_;
static std::mutex clients_mtx_;
static int server_fd_ = -1;

static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const unsigned char* data, size_t len) {
    std::string ret;
    int i = 0, j = 0;
    unsigned char char_array_3[3], char_array_4[4];

    while (len--) {
        char_array_3[i++] = *(data++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for (i = 0; i < 4; i++) ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    if (i) {
        for (j = i; j < 3; j++) char_array_3[j] = '\0';
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        for (j = 0; j < i + 1; j++) ret += base64_chars[char_array_4[j]];
        while (i++ < 3) ret += '=';
    }
    return ret;
}

static bool do_handshake(int client_fd) {
    char buffer[4096];
    ssize_t n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) return false;
    buffer[n] = '\0';

    std::string request(buffer);
    size_t key_pos = request.find("Sec-WebSocket-Key: ");
    if (key_pos == std::string::npos) return false;

    size_t key_start = key_pos + 19;
    size_t key_end = request.find("\r\n", key_start);
    std::string key = request.substr(key_start, key_end - key_start);

    std::string accept_key = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(accept_key.c_str()), accept_key.length(), hash);
    std::string accept_hash = base64_encode(hash, SHA_DIGEST_LENGTH);

    std::ostringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n"
             << "Upgrade: websocket\r\n"
             << "Connection: Upgrade\r\n"
             << "Sec-WebSocket-Accept: " << accept_hash << "\r\n\r\n";

    std::string resp_str = response.str();
    return send(client_fd, resp_str.c_str(), resp_str.length(), 0) > 0;
}

static void client_handler(int client_fd, std::atomic<bool>* running) {
    char buffer[4096];
    while (running->load()) {
        ssize_t n = recv(client_fd, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
    }
    
    {
        std::lock_guard<std::mutex> lock(clients_mtx_);
        clients_.erase(client_fd);
    }
    close(client_fd);
}

WsServer::WsServer(int port) : port_(port) {}

void WsServer::start() {
    if (running_.load()) return;

    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) return;

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(server_fd_);
        return;
    }

    if (listen(server_fd_, 10) < 0) {
        close(server_fd_);
        return;
    }

    running_.store(true);

    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    std::cout << "[GUI] WebSocket listening on 0.0.0.0:" << port_ << std::endl;
    std::cout << "[GUI]   Hostname: " << hostname << std::endl;
    std::cout << "[GUI]   Access: http://YOUR_IP:" << port_ << std::endl;

    accept_thread_ = std::thread(&WsServer::acceptLoop, this);
    publish_thread_ = std::thread(&WsServer::publishLoop, this);
}

void WsServer::stop() {
    if (!running_.load()) return;
    running_.store(false);
    
    if (server_fd_ >= 0) {
        close(server_fd_);
    }
    
    if (accept_thread_.joinable()) accept_thread_.join();
    if (publish_thread_.joinable()) publish_thread_.join();
    
    std::lock_guard<std::mutex> lock(clients_mtx_);
    for (auto fd : clients_) {
        close(fd);
    }
    clients_.clear();
}

void WsServer::acceptLoop() {
    while (running_.load()) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (running_.load()) {
                std::cerr << "[WsServer] accept() error" << std::endl;
            }
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        std::cout << "[WsServer] Client: " << client_ip << std::endl;

        if (!do_handshake(client_fd)) {
            close(client_fd);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(clients_mtx_);
            clients_.insert(client_fd);
        }

        std::thread(client_handler, client_fd, &running_).detach();
    }
}

void WsServer::publishLoop() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void WsServer::broadcast(const std::string& message) {
    std::vector<uint8_t> frame;
    frame.push_back(0x81);
    
    if (message.length() < 126) {
        frame.push_back(static_cast<uint8_t>(message.length()));
    } else if (message.length() < 65536) {
        frame.push_back(126);
        frame.push_back((message.length() >> 8) & 0xFF);
        frame.push_back(message.length() & 0xFF);
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; i--) {
            frame.push_back((message.length() >> (i * 8)) & 0xFF);
        }
    }
    
    frame.insert(frame.end(), message.begin(), message.end());

    std::lock_guard<std::mutex> lock(clients_mtx_);
    for (auto fd : clients_) {
        send(fd, frame.data(), frame.size(), MSG_NOSIGNAL);
    }
}
