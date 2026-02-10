#include "gui/GUIBroadcaster.hpp"
#include "gui/WsServer.hpp"
#include "gui/ExecutionSnapshot.hpp"
#include "shadow/MultiSymbolExecutor.hpp"
#include <thread>
#include <chrono>

extern WsServer* g_wsServer;
extern shadow::MultiSymbolExecutor* g_executor;

namespace Chimera {

GUIBroadcaster::GUIBroadcaster() : ws_(nullptr) {}

GUIBroadcaster::~GUIBroadcaster() {
    stop();
}

void GUIBroadcaster::start() {
    ws_ = g_wsServer;
    
    std::thread([this]() {
        while (true) {
            if (!g_executor) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                continue;
            }

            gui::ExecutionSnapshot snap;
            snap.ts = std::chrono::system_clock::now().time_since_epoch().count() / 1000000000;

            // XAU data
            auto* xau_exec = g_executor->getExecutor("XAUUSD");
            if (xau_exec) {
                auto& xau = snap.symbols["XAUUSD"];
                xau.bid = xau_exec->getLastBid();
                xau.ask = xau_exec->getLastAsk();
                xau.spread = xau_exec->getSpread();
                xau.latency_ms = xau_exec->getLatencyMs();
                xau.trades = xau_exec->getTradesThisHour();
                xau.rejects = xau_exec->getTotalRejections();
                xau.legs = xau_exec->getActiveLegs();
                xau.session = "NY";
                xau.regime = "TREND";
                
                int legs = xau_exec->getActiveLegs();
                xau.state = (legs > 0) ? "IN_POSITION" : "OPEN";
                
                xau.gates["session"] = gui::Gate(true, "active", "");
                xau.gates["spread"] = gui::Gate(xau.spread < 0.50, "OK", xau.spread >= 0.50 ? "spread too wide" : "");
                xau.gates["latency"] = gui::Gate(xau.latency_ms < 50.0, "OK", xau.latency_ms >= 50.0 ? "latency high" : "");
                xau.gates["edge"] = gui::Gate(true, "checking", "");
                xau.gates["cooldown"] = gui::Gate(true, "ready", "");

                xau.cost.total_bps = 28.0;
                xau.edge.raw_bps = 31.5;
                xau.edge.latency_adj_bps = 26.2;
                xau.edge.required_bps = 28.0;

                xau.impulse.raw = 1.42;
                xau.impulse.latency_adj = 0.88;
                xau.impulse.min_required = 1.10;

                xau.pnl.shadow = xau_exec->getRealizedPnL();
                xau.pnl.cash = 0.0;
            }

            // XAG data
            auto* xag_exec = g_executor->getExecutor("XAGUSD");
            if (xag_exec) {
                auto& xag = snap.symbols["XAGUSD"];
                xag.bid = xag_exec->getLastBid();
                xag.ask = xag_exec->getLastAsk();
                xag.spread = xag_exec->getSpread();
                xag.latency_ms = xag_exec->getLatencyMs();
                xag.trades = xag_exec->getTradesThisHour();
                xag.rejects = xag_exec->getTotalRejections();
                xag.legs = xag_exec->getActiveLegs();
                xag.session = "NY";
                xag.regime = "MEAN";
                
                int legs = xag_exec->getActiveLegs();
                xag.state = (legs > 0) ? "IN_POSITION" : "OPEN";

                xag.gates["session"] = gui::Gate(true, "active", "");
                xag.gates["spread"] = gui::Gate(xag.spread < 0.05, "OK", xag.spread >= 0.05 ? "spread too wide" : "");
                xag.gates["latency"] = gui::Gate(xag.latency_ms < 50.0, "OK", xag.latency_ms >= 50.0 ? "latency high" : "");
                xag.gates["edge"] = gui::Gate(true, "checking", "");
                xag.gates["cooldown"] = gui::Gate(true, "ready", "");

                xag.cost.total_bps = 15.0;
                xag.edge.raw_bps = 12.0;
                xag.edge.latency_adj_bps = 10.0;
                xag.edge.required_bps = 15.0;

                xag.impulse.raw = 0.65;
                xag.impulse.latency_adj = 0.52;
                xag.impulse.min_required = 0.80;

                xag.pnl.shadow = xag_exec->getRealizedPnL();
                xag.pnl.cash = 0.0;
            }

            snap.governor.daily_dd = "OK";
            snap.governor.hourly_loss = "OK";
            snap.governor.reject_rate = "LOW";
            snap.governor.action = "NONE";

            snap.connections.fix = true;
            snap.connections.ctrader = true;

            std::string json = gui::EmitJSON(snap);
            
            if (ws_) {
                ws_->broadcast(json);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }).detach();
}

void GUIBroadcaster::stop() {
}

} // namespace Chimera
