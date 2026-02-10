#pragma once

class WsServer;

namespace Chimera {

class GUIBroadcaster {
public:
    GUIBroadcaster();
    ~GUIBroadcaster();

    void start();
    void stop();

    // Compatibility stubs
    void broadcastTrade(const char* sym, const char* side, double sz, double px, double pnl) {}
    void updatePrice(double b, double a) {}
    void updatePnL(double p) {}
    void setConnected(bool v) {}

private:
    ::WsServer* ws_ = nullptr;
};

} // namespace Chimera
