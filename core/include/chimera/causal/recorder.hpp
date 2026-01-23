#pragma once

#include "chimera/causal/events.hpp"
#include <fstream>
#include <mutex>
#include <atomic>

namespace chimera::causal {

class Recorder {
public:
    explicit Recorder(const std::string& base_path);
    ~Recorder();

    event_id_t next_id();

    void record(const TickEvent& e);
    void record(const DecisionEvent& e);
    void record(const RiskEvent& e);
    void record(const OrderIntentEvent& e);
    void record(const VenueAckEvent& e);
    void record(const FillEvent& e);
    void record(const PnLAttributionEvent& e);

    void flush();

private:
    std::mutex mtx;
    std::ofstream bin;
    std::ofstream jsonl;
    std::atomic<event_id_t> counter;

    template<typename T>
    void write(const T& e);
};

}
