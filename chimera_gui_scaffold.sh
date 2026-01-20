#!/usr/bin/env bash
set -e

echo "[CHIMERA] CLEAN GUI STACK — SINGLE PASS"

cd "$(dirname "$0")"

# ----------------------------
# STRUCTURE
# ----------------------------
rm -rf gui
mkdir -p gui/include gui/src gui/web

# ----------------------------
# HTTP HEADER
# ----------------------------
echo "[CHIMERA] Fetching httplib..."
curl -L -o gui/include/httplib.h https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h

# ----------------------------
# SNAPSHOT BUS
# ----------------------------
cat > gui/include/gui_snapshot_bus.hpp << 'EOT1'
#pragma once
#include <atomic>
#include <string>

class GuiSnapshotBus {
public:
    static GuiSnapshotBus& instance();

    void update(const std::string& snapshot);
    std::string get() const;

private:
    GuiSnapshotBus();
    std::atomic<unsigned long long> version_;
    std::string buffer_;
};
EOT1

cat > gui/src/gui_snapshot_bus.cpp << 'EOT2'
#include "gui_snapshot_bus.hpp"
#include <mutex>

static std::mutex g_lock;

GuiSnapshotBus::GuiSnapshotBus() : version_(0), buffer_("BOOTING\n") {}

GuiSnapshotBus& GuiSnapshotBus::instance() {
    static GuiSnapshotBus bus;
    return bus;
}

void GuiSnapshotBus::update(const std::string& snapshot) {
    std::lock_guard<std::mutex> lock(g_lock);
    buffer_ = snapshot;
    version_.fetch_add(1);
}

std::string GuiSnapshotBus::get() const {
    std::lock_guard<std::mutex> lock(g_lock);
    return buffer_;
}
EOT2

# ----------------------------
# LIVE SERVER
# ----------------------------
cat > gui/include/live_operator_server.hpp << 'EOT3'
#pragma once
#include <thread>

class LiveOperatorServer {
public:
    explicit LiveOperatorServer(int port = 8080);
    void start();
    void stop();

private:
    int port_;
    std::thread server_thread_;
};
EOT3

cat > gui/src/live_operator_server.cpp << 'EOT4'
#include "live_operator_server.hpp"
#include "gui_snapshot_bus.hpp"
#include "httplib.h"

#include <chrono>
#include <fstream>
#include <sstream>

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

        svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
            std::string html = load_file("gui/web/index.html");
            res.set_content(html, "text/html");
        });

        svr.Get("/stream", [](const httplib::Request&, httplib::Response& res) {
            res.set_header("Content-Type", "text/event-stream");
            res.set_header("Cache-Control", "no-cache");
            res.set_header("Connection", "keep-alive");

            std::string last;

            while (true) {
                std::string now = GuiSnapshotBus::instance().get();
                if (now != last) {
                    res.body += "data: " + now + "\n\n";
                    last = now;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        });

        svr.listen("0.0.0.0", port_);
    });
}

void LiveOperatorServer::stop() {
    if (server_thread_.joinable())
        server_thread_.join();
}
EOT4

# ----------------------------
# WEB UI
# ----------------------------
cat > gui/web/index.html << 'EOT5'
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>CHIMERA LIVE</title>
<style>
body {
  background: #000;
  color: #0f0;
  font-family: monospace;
}
pre {
  white-space: pre-wrap;
  font-size: 14px;
}
</style>
</head>
<body>
<h2>CHIMERA — LIVE OPERATOR FEED</h2>
<pre id="feed">CONNECTING...</pre>

<script>
const feed = document.getElementById("feed");
const es = new EventSource("/stream");

es.onmessage = function(e) {
  feed.textContent = e.data;
};

es.onerror = function() {
  feed.textContent = "DISCONNECTED";
};
</script>
</body>
</html>
EOT5

# ----------------------------
# CMAKE GUI LIB
# ----------------------------
cat > gui/CMakeLists.txt << 'EOT6'
add_library(chimera_gui
    src/gui_snapshot_bus.cpp
    src/live_operator_server.cpp
)

target_include_directories(chimera_gui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
EOT6

# ----------------------------
# ROOT CMAKE LINK
# ----------------------------
if ! grep -q chimera_gui CMakeLists.txt; then
  echo "[CHIMERA] Wiring GUI into root CMake..."
  sed -i "/target_link_libraries.*chimera/ s/$/ chimera_gui/" CMakeLists.txt
fi

# ----------------------------
# MAIN INTEGRATION NOTE
# ----------------------------
echo ""
echo "[CHIMERA] REQUIRED ONE-TIME EDIT IN main.cpp:"
echo "#include \"gui_snapshot_bus.hpp\""
echo "#include \"live_operator_server.hpp\""
echo "static LiveOperatorServer gui(8080);"
echo "gui.start();"
echo "GuiSnapshotBus::instance().update(flow_string);"
echo ""

# ----------------------------
# BUILD
# ----------------------------
echo "[CHIMERA] Rebuilding..."
rm -rf build
cmake -B build
cmake --build build -j

echo "[CHIMERA] Launching..."
./build/chimera
