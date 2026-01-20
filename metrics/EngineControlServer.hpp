#pragma once

#include "../risk/KillSwitchGovernor.hpp"

class EngineControlServer {
public:
    EngineControlServer(
        KillSwitchGovernor* kill_gov,
        int prometheus_port
    )
        : kill_gov_(kill_gov),
          port_(prometheus_port)
    {
        (void)kill_gov_;
        (void)port_;
    }

    void start() {
        // HTTP/metrics wiring can be reattached later
    }

private:
    KillSwitchGovernor* kill_gov_;
    int port_;
};

