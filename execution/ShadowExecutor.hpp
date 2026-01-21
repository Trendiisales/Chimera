#pragma once
#include <string>

class ShadowExecutor {
public:
    void onIntent(
        const std::string& engine,
        const std::string& symbol,
        double bps,
        double latency_ms
    );
};
