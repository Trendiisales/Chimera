#pragma once
#include "core/spine_replay.hpp"
#include <functional>

class CounterfactualEngine {
public:
    using Mutator = std::function<std::string(const SpineEvent&)>;

    CounterfactualEngine(const std::string& replay_file,
                         Mutator mutator);

    void run();

private:
    SpineReplay replay;
    Mutator mutate;
};
