#pragma once
#include "ReplayLog.hpp"

namespace chimera {

class ReplayEngine {
public:
    explicit ReplayEngine(const ReplayLog& log);

    void replay();

private:
    const ReplayLog& m_log;
};

}
