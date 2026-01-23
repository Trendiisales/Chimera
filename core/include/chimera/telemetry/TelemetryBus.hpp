#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <mutex>
#include "chimera/infra/Clock.hpp"

namespace chimera::telemetry {

struct TelemetryEvent {
    std::string topic;
    std::string payload;
    infra::MonoTime ts;
};

using Subscriber = std::function<void(const TelemetryEvent&)>;

class TelemetryBus {
public:
    void publish(const std::string& topic, const std::string& payload) {
        TelemetryEvent ev{topic, payload, infra::now()};
        std::lock_guard<std::mutex> lock(mu);
        auto it = subs.find(topic);
        if (it == subs.end()) return;
        for (auto& fn : it->second) {
            fn(ev);
        }
    }

    void subscribe(const std::string& topic, Subscriber fn) {
        std::lock_guard<std::mutex> lock(mu);
        subs[topic].push_back(std::move(fn));
    }

private:
    std::mutex mu;
    std::unordered_map<std::string, std::vector<Subscriber>> subs;
};

} // namespace chimera::telemetry
