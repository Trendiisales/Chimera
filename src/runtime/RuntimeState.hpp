#pragma once
#include <memory>
#include "runtime/Context.hpp"

namespace chimera {

// Shared runtime state - owned by all threads via shared_ptr
// Eliminates dangling references and lifetime bugs
struct RuntimeState {
    std::shared_ptr<Context> ctx;
    
    RuntimeState() : ctx(std::make_shared<Context>()) {}
};

} // namespace chimera
