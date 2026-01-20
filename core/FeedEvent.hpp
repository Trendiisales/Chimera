#pragma once
#include "TickSnapshot.hpp"
#include "../tier3/TickData.hpp"
#include <string>

class FeedEventBus {
public:
    static FeedEventBus& instance() {
        static FeedEventBus bus;
        return bus;
    }

    bool poll(const std::string&, TickSnapshot&) {
        return false;
    }

    bool poll(const std::string&, tier3::TickData&) {
        return false;
    }
};
