#pragma once
#include <cstdint>

namespace chimera {

enum class SessionType {
    ASIA = 0,
    LONDON = 1,
    NY = 2,
    OVERLAP = 3
};

class SessionBias {
public:
    SessionBias();

    SessionType update(uint64_t utc_ts_ns);
    double bias() const;
    SessionType session() const;

private:
    SessionType m_session;
    double m_bias;
};

}
