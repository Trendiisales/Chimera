#pragma once
#include "core/contract.hpp"

class GUIServer {
    int port_;
    ChimeraTelemetry& telemetry_;

public:
    GUIServer(int port, ChimeraTelemetry& t);
    void run();
};
