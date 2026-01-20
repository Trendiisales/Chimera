#include "live_operator_server.hpp"
#include "gui_snapshot_bus.hpp"
#include "httplib.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

static std::string load_file(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

LiveOperatorServer::LiveOperatorServer(int port)
    : port_(port) {}

void LiveOperatorServer::start() {
    server_thread_ = std::thread([this]() {
        httplib::Server svr;

        // Root page
        svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
            std::string html = load_file("gui/web/index.html");
            res.set_content(html, "text/html");
        });

        // SSE stream — provider runs its own loop (portable behavior)
        svr.Get("/stream", [](const httplib::Request&, httplib::Response& res) {
            res.set_header("Content-Type", "text/event-stream");
            res.set_header("Cache-Control", "no-cache");
            res.set_header("Connection", "keep-alive");

            res.set_chunked_content_provider(
                "text/event-stream",
                [](size_t, httplib::DataSink& sink) {
                    std::string last;

                    while (true) {
                        std::string now = GuiSnapshotBus::instance().get();
                        if (now != last) {
                            std::string msg = "data: " + now + "\n\n";
                            sink.write(msg.data(), msg.size());
                            last = now;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    }

                    return false; // unreachable, but required by signature
                }
            );
        });

        svr.listen("0.0.0.0", port_);
    });
}

void LiveOperatorServer::stop() {
    if (server_thread_.joinable())
        server_thread_.join();
}
