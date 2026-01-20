#include "TelemetryServer.hpp"
#include "TelemetryBus.hpp"

#include <boost/asio.hpp>
#include <sstream>

using boost::asio::ip::tcp;

TelemetryServer::TelemetryServer(int port)
    : port_(port) {}

void TelemetryServer::start() {
    running_ = true;
    worker_ = std::thread(&TelemetryServer::run, this);
}

void TelemetryServer::stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

static std::string jsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"') o += "\\\"";
        else o += c;
    }
    return o;
}

void TelemetryServer::run() {
    boost::asio::io_context io;
    tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), port_));

    while (running_) {
        tcp::socket socket(io);
        acceptor.accept(socket);

        auto engines = TelemetryBus::instance().engines();
        auto trades = TelemetryBus::instance().trades();

        std::ostringstream body;
        body << "{ \"engines\": [";
        for (size_t i = 0; i < engines.size(); i++) {
            const auto& e = engines[i];
            body << "{";
            body << "\"name\":\"" << jsonEscape(e.name) << "\",";
            body << "\"net_bps\":" << e.net_bps << ",";
            body << "\"dd_bps\":" << e.dd_bps << ",";
            body << "\"trades\":" << e.trades << ",";
            body << "\"fees\":" << e.fees << ",";
            body << "\"alloc\":" << e.alloc << ",";
            body << "\"leverage\":" << e.leverage << ",";
            body << "\"status\":\"" << e.status << "\"";
            body << "}";
            if (i + 1 < engines.size()) body << ",";
        }
        body << "], \"trades\": [";

        for (size_t i = 0; i < trades.size(); i++) {
            const auto& t = trades[i];
            body << "{";
            body << "\"engine\":\"" << jsonEscape(t.engine) << "\",";
            body << "\"symbol\":\"" << jsonEscape(t.symbol) << "\",";
            body << "\"side\":\"" << jsonEscape(t.side) << "\",";
            body << "\"bps\":" << t.bps << ",";
            body << "\"latency_ms\":" << t.latency_ms << ",";
            body << "\"lev\":" << t.leverage;
            body << "}";
            if (i + 1 < trades.size()) body << ",";
        }
        body << "] }";

        std::ostringstream resp;
        resp << "HTTP/1.1 200 OK\r\n";
        resp << "Content-Type: application/json\r\n";
        resp << "Access-Control-Allow-Origin: *\r\n";
        resp << "Content-Length: " << body.str().size() << "\r\n\r\n";
        resp << body.str();

        boost::asio::write(socket, boost::asio::buffer(resp.str()));
    }
}
