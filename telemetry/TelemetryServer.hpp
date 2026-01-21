#pragma once
#include <ostream>

class TelemetryServer {
public:
    static void handleRequest(std::ostream& body);
};

void runTelemetryServer(int port);
