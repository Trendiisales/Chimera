#pragma once
#include <thread>
#include <atomic>
#include <sstream>
#include <vector>
#include <netinet/in.h>
#include <unistd.h>

#include "../allocator/CapitalAllocator.hpp"
#include "../ledger/TradeLedger.hpp"

class GuiServer {
public:
    GuiServer(int port,
              CapitalAllocator* alloc,
              TradeLedger* ledger)
        : port_(port), alloc_(alloc), ledger_(ledger) {}

    void start() {
        running_ = true;
        thread_ = std::thread(&GuiServer::run, this);
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

private:
    void run() {
        int server_fd = socket(AF_INET, SOCK_STREAM, 0);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);

        bind(server_fd, (sockaddr*)&addr, sizeof(addr));
        listen(server_fd, 10);

        while (running_) {
            int client = accept(server_fd, nullptr, nullptr);
            if (client < 0) continue;

            auto buckets = alloc_->rank(100.0);
            auto trades = ledger_->snapshot();

            std::stringstream body;
            body << "<html><head><meta http-equiv='refresh' content='1'></head><body>";
            body << "<h2>CHIMERA LIVE</h2>";
            body << "<h3>Capital Flow</h3><pre>";
            for (auto& b : buckets)
                body << b.name << " NET=" << b.net << " ALLOC=" << b.allocation << "\n";
            body << "</pre>";

            body << "<h3>Trades</h3><pre>";
            for (auto& t : trades)
                body << t.symbol << " " << t.engine
                     << " pnl=" << t.pnl << "\n";
            body << "</pre></body></html>";

            std::string html = body.str();
            std::stringstream hdr;
            hdr << "HTTP/1.1 200 OK\r\n"
                << "Content-Type: text/html\r\n"
                << "Content-Length: " << html.size() << "\r\n\r\n";

            std::string out = hdr.str() + html;
            send(client, out.c_str(), out.size(), 0);
            close(client);
        }
        close(server_fd);
    }

    int port_;
    CapitalAllocator* alloc_;
    TradeLedger* ledger_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};
