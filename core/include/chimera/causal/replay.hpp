#pragma once

#include "chimera/causal/events.hpp"
#include <vector>
#include <string>
#include <memory>
#include <queue>

namespace chimera::causal {

struct ReplayStream {
    std::vector<EventHeader> headers;
    std::vector<std::unique_ptr<char[]>> payloads;
    
    size_t size() const { return headers.size(); }
    bool empty() const { return headers.empty(); }
};

class ReplayEngine {
public:
    explicit ReplayEngine(const std::string& path);
    
    ReplayStream load();
    ReplayStream load_range(uint64_t start_ts_ns, uint64_t end_ts_ns);
    
    size_t count_events(EventType type) const;
    
private:
    std::string file_path;
    size_t file_size;
    
    void validate_file();
};

// Replay Bus for deterministic decision replay
class ReplayBus {
public:
    void push(const DecisionEvent& ev) {
        decisions.push(ev);
    }

    bool hasNext() const {
        return !decisions.empty();
    }

    DecisionEvent next() {
        auto ev = decisions.front();
        decisions.pop();
        return ev;
    }

private:
    std::queue<DecisionEvent> decisions;
};

}
