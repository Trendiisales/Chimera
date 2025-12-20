#pragma once
#include "arbiter/VenueHealth.hpp"

namespace Chimera {

class Arbiter {
public:
    explicit Arbiter(VenueHealth& vh);
    bool allow_execution(uint64_t latency_us);
    void on_reject();
private:
    VenueHealth& vh_;
};

}
