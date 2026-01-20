#pragma once
#include <string>

class ShadowExecutor {
public:
    ShadowExecutor() = default;

    void onIntent(
        const std::string& engine,
        const std::string& symbol,
        double bps,
        double latency_ms
    );
};
