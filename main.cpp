#include "gui/live_operator_server.cpp"
#include "telemetry/telemetry_boot.hpp"
#include <iostream>
#include <chrono>
#include <thread>

#include "allocator/CapitalAllocator.hpp"
#include "ledger/TradeLedger.hpp"
#include "gui/GuiServer.hpp"

int main() {
    start_operator_console(8080);
    startTelemetry(9090);
    std::cout << "[CHIMERA] MODE B LIVE STACK | DRY | GUI ACTIVE\n";

    CapitalAllocator allocator;
    TradeLedger ledger("logs/trades.jsonl");

    GuiServer gui(8080, &allocator, &ledger);
    gui.start();

    uint64_t tick = 0;

    while (true) {
        allocator.updateMetric("ETH_PERP", 1.2, 0.1, 0.05, 0.02, 0.1, 0.05);
        allocator.updateMetric("BTC_PERP", 0.9, 0.1, 0.05, 0.03, 0.1, 0.05);
        allocator.updateMetric("SOL_SPOT", 0.6, 0.05, 0.0, 0.01, 0.05, 0.02);

        if (tick % 60 == 0) {
            auto ranked = allocator.rank(100.0);
            std::cout << "[FLOW] ";
            for (auto& b : ranked)
                std::cout << b.name << "=" << (int)b.allocation << "% ";
            std::cout << "\n";
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
        tick++;
    }
}

#include "telemetry/FlightDeckServer.cpp"


#include "replay/TickRecorder.hpp"
#include "replay/TickReplayer.hpp"
#include "learning/AutoKillLearner.hpp"
#include "allocator/CapitalRotationAI.hpp"
#include "learning/MonteCarloRisk.hpp"

static TickRecorder* REC = nullptr;
static AutoKillLearner KILL_AI;
static CapitalRotationAI ROTATE_AI;
static MonteCarloRisk MC_RISK;

void chimera_telemetry_tick(const std::string& sym, const tier3::TickData& t) {
    if (REC) REC->record(sym, t);

    double edge = t.ofi_z;
    double lat = t.spread_bps * 10.0;

    KILL_AI.observe(sym, edge, lat);

    if (KILL_AI.shouldKill(sym)) {
        TelemetryBus::instance().push("RISK", {
            {"symbol", sym},
            {"state", "KILLED"}
        });
    }

    ROTATE_AI.update(sym, edge);
    MC_RISK.sample(edge);
}

void start_time_machine(const std::string& file) {
    REC = new TickRecorder(file);
}
