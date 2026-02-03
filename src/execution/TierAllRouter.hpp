#pragma once
#include "execution/ExecutionRouter.hpp"
#include <memory>

namespace chimera {

class TierAllRouter : public ExecutionRouter {
public:
    TierAllRouter(std::shared_ptr<Context> ctx) : ExecutionRouter(std::move(ctx)) {
        std::cout << "[TIER_ALL] Router initialized (shared ownership, thread-safe)" << std::endl;
    }
};

} // namespace chimera
