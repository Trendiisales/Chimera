#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio.hpp>
#include <thread>
#include <sstream>
#include "../core/Telemetry.hpp"

using tcp = boost::asio::ip::tcp;
namespace http = boost::beast::http;
namespace ws = boost::beast::websocket;

std::string render_json(const TelemetryFrame& f) {
    std::ostringstream o;
    o << "{";
    o << "\"seq\":" << f.seq << ",";
    o << "\"mode\":\"" << f.mode << "\",";
    o << "\"risk_scale\":" << f.risk_scale << ",";
    o << "\"kill\":" << (f.kill ? "true" : "false") << ",";
    o << "\"daily_pnl\":" << f.daily_pnl << ",";

    o << "\"symbols\":[";
    for (size_t i = 0; i < f.symbols.size(); i++) {
        auto& s = f.symbols[i];
        o << "{";
        o << "\"symbol\":\"" << s.symbol << "\",";
        o << "\"engine\":\"" << s.engine << "\",";
        o << "\"regime\":\"" << s.regime << "\",";
        o << "\"edge\":" << s.edge << ",";
        o << "\"expectancy\":" << s.expectancy << ",";
        o << "\"alloc\":" << s.alloc << ",";
        o << "\"net\":" << s.net << ",";
        o << "\"latency_ms\":" << s.latency_ms << ",";
        o << "\"spread\":" << s.spread << ",";
        o << "\"ofi\":" << s.ofi;
        o << "}";
        if (i + 1 < f.symbols.size()) o << ",";
    }
    o << "],";

    o << "\"trades\":[";
    for (size_t i = 0; i < f.trades.size(); i++) {
        auto& t = f.trades[i];
        o << "{";
        o << "\"time\":\"" << t.time << "\",";
        o << "\"symbol\":\"" << t.symbol << "\",";
        o << "\"engine\":\"" << t.engine << "\",";
        o << "\"side\":\"" << t.side << "\",";
        o << "\"qty\":" << t.qty << ",";
        o << "\"entry\":" << t.entry << ",";
        o << "\"exit\":" << t.exit << ",";
        o << "\"pnl\":" << t.pnl << ",";
        o << "\"latency_ms\":" << t.latency_ms << ",";
        o << "\"reason\":\"" << t.reason << "\"";
        o << "}";
        if (i + 1 < f.trades.size()) o << ",";
    }
    o << "]";
    o << "}";
    return o.str();
}

void serve(unsigned short port) {
    boost::asio::io_context ioc;
    tcp::acceptor acceptor(ioc, {tcp::v4(), port});

    for (;;) {
        tcp::socket socket(ioc);
        acceptor.accept(socket);

        std::thread([s = std::move(socket)]() mutable {
            boost::beast::flat_buffer buffer;
            http::request<http::string_body> req;
            http::read(s, buffer, req);

            if (req.target() == "/ws") {
                ws::stream<tcp::socket> sock(std::move(s));
                sock.accept();

                while (true) {
                    auto snap = TelemetryBus::instance().snapshot();
                    sock.write(boost::asio::buffer(render_json(snap)));
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            }

            std::string html =
R"(<!DOCTYPE html>
<html>
<head>
<title>CHIMERA OPERATOR</title>
<style>
body { background:#111; color:#0f0; font-family:monospace; }
table { border-collapse: collapse; width:100%; }
td, th { border:1px solid #0f0; padding:4px; }
</style>
</head>
<body>
<h1>CHIMERA OPERATOR CONSOLE</h1>
<div id="risk"></div>

<h2>Capital Flow</h2>
<table id="symbols"></table>

<h2>Trades (Last 50)</h2>
<table id="trades"></table>

<script>
let ws = new WebSocket("ws://" + location.host + "/ws");

ws.onmessage = (e) => {
  let d = JSON.parse(e.data);

  document.getElementById("risk").innerText =
    "MODE=" + d.mode +
    " | SCALE=" + d.risk_scale +
    " | KILL=" + d.kill +
    " | DAILY_PNL=" + d.daily_pnl;

  let s = "<tr><th>SYMBOL</th><th>ENGINE</th><th>REGIME</th><th>EDGE</th><th>EXP</th><th>ALLOC</th><th>NET</th><th>LAT(ms)</th><th>SPR</th><th>OFI</th></tr>";
  d.symbols.forEach(x => {
    s += `<tr>
      <td>${x.symbol}</td>
      <td>${x.engine}</td>
      <td>${x.regime}</td>
      <td>${x.edge.toFixed(3)}</td>
      <td>${x.expectancy.toFixed(3)}</td>
      <td>${x.alloc.toFixed(2)}</td>
      <td>${x.net.toFixed(2)}</td>
      <td>${x.latency_ms.toFixed(2)}</td>
      <td>${x.spread.toFixed(5)}</td>
      <td>${x.ofi.toFixed(3)}</td>
    </tr>`;
  });
  document.getElementById("symbols").innerHTML = s;

  let t = "<tr><th>TIME</th><th>SYM</th><th>ENG</th><th>SIDE</th><th>QTY</th><th>ENTRY</th><th>EXIT</th><th>PNL</th><th>LAT</th><th>WHY</th></tr>";
  d.trades.forEach(x => {
    t += `<tr>
      <td>${x.time}</td>
      <td>${x.symbol}</td>
      <td>${x.engine}</td>
      <td>${x.side}</td>
      <td>${x.qty}</td>
      <td>${x.entry}</td>
      <td>${x.exit}</td>
      <td>${x.pnl.toFixed(2)}</td>
      <td>${x.latency_ms.toFixed(2)}</td>
      <td>${x.reason}</td>
    </tr>`;
  });
  document.getElementById("trades").innerHTML = t;
};
</script>
</body>
</html>)";

            http::response<http::string_body> res{
                http::status::ok, req.version()
            };
            res.set(http::field::content_type, "text/html");
            res.body() = html;
            res.prepare_payload();
            http::write(s, res);
        }).detach();
    }
}

int main() {
    serve(8080);
}
