#pragma once

#include "chimera/mode/RunMode.hpp"
#include "chimera/infra/Clock.hpp"

namespace chimera::contract {

// All strategies must implement this interface
// to participate in replay
struct ReplayableStrategy {
    // Called when entering replay mode
    virtual void on_replay_start() = 0;
    
    // Called when exiting replay mode
    virtual void on_replay_end() = 0;
    
    // Process replayed event
    // timestamp: event time from replay log
    // data: event-specific data
    virtual void on_replay_event(
        infra::MonoTime timestamp,
        const void* data,
        size_t size
    ) = 0;
    
    // Validate that strategy is in valid state for replay
    virtual bool validate_replay_ready() const = 0;
    
    virtual ~ReplayableStrategy() = default;
};

// Replay bus must implement this
struct ReplayBus {
    // Start replay session
    virtual void start_replay() = 0;
    
    // End replay session
    virtual void end_replay() = 0;
    
    // Register strategy for replay events
    virtual void register_strategy(ReplayableStrategy* strategy) = 0;
    
    // Unregister strategy
    virtual void unregister_strategy(ReplayableStrategy* strategy) = 0;
    
    virtual ~ReplayBus() = default;
};

} // namespace chimera::contract
