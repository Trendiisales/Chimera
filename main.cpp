#include "shadow/MultiSymbolExecutor.hpp"
#include "core/Globals.hpp"
#include "gui/GUIBroadcaster.hpp"
#include "gui/WsServer.hpp"

#include <iostream>
#include <chrono>
#include <thread>

using namespace shadow;

extern WsServer* g_wsServer;

int main() {
    std::cout << "[CHIMERA] Engine booting...\n";

    static MultiSymbolExecutor executor;
    g_executor = &executor;

    executor.addSymbol({ "XAUUSD" }, ExecMode::SHADOW);
    executor.addSymbol({ "XAGUSD" }, ExecMode::SHADOW);

    static WsServer ws(7777);
    g_wsServer = &ws;
    ws.start();

    Chimera::GUIBroadcaster broadcaster;
    broadcaster.start();

    std::cout << "[CHIMERA] Running.\n";
    std::cout << "[CHIMERA] WebSocket GUI on port 7777\n";
    std::cout << "[CHIMERA] Connect dashboard to ws://YOUR_SERVER_IP:7777\n";

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;
}
