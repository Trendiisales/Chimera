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

// Always return valid JSON
static std::string json_wrap(const std::string& raw) {
    if (raw.empty()) {
        return "{\"ts\":\"BOOT\",\"flow\":{},\"latency_ms\":0,\"spread_bps\":0,\"pnl\":{\"session\":0,\"today\":0},\"risk\":\"CONNECTING\",\"engines\":[],\"trades\":[]}";
    }
    // If it's already JSON, pass through
    if (!raw.empty() && raw.front() == '{') return raw;

    // Otherwise wrap plain text into JSON
    std::ostringstream ss;
    ss << "{"
       << "\"ts\":\"TEXT\","
       << "\"flow\":{},"
       << "\"latency_ms\":0,"
       << "\"spread_bps\":0,"
       << "\"pnl\":{\"session\":0,\"today\":0},"
       << "\"risk\":\"TEXT\","
       << "\"engines\":[],"
       << "\"trades\":[],"
       << "\"message\":\"";

    for (char c : raw) {
        if (c == '"' || c == '\\') ss << '\\';
        ss << c;
    }
    ss << "\"}";
    return ss.str();
}

LiveOperatorServer::LiveOperatorServer(int port)
    : port_(port) {}

void LiveOperatorServer::start() {
    server_thread_ = std::thread([this]() {
        httplib::Server svr;

        svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
            std::string html = load_file("../gui/web/index.html");
            res.set_content(html, "text/html");
        });

        svr.Get("/stream", [](const httplib::Request&, httplib::Response& res) {
            res.set_header("Content-Type", "text/event-stream");
            res.set_header("Cache-Control", "no-cache");
            res.set_header("Connection", "keep-alive");

            res.set_chunked_content_provider(
                "text/event-stream",
                [](size_t, httplib::DataSink& sink) {
                    std::string last;

                    while (true) {
                        std::string raw = GuiSnapshotBus::instance().get();
                        std::string json = json_wrap(raw);

                        if (json != last) {
                            std::string msg = "data: " + json + "\n\n";
                            sink.write(msg.data(), msg.size());
                            last = json;
                        }

                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    }
                    return false;
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
