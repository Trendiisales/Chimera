#include "SessionBias.hpp"
#include <ctime>

namespace chimera {

SessionBias::SessionBias()
    : m_session(SessionType::ASIA), m_bias(0.5) {}

SessionType SessionBias::update(uint64_t utc_ts_ns) {
    time_t sec = time_t(utc_ts_ns / 1000000000ULL);
    tm t;
    #ifdef _WIN32
gmtime_s(&t, &sec);
#else
gmtime_r(&sec, &t);
#endif;

    int hour = t.tm_hour;

    if (hour >= 0 && hour < 7) {
        m_session = SessionType::ASIA;
        m_bias = 0.4;
    } else if (hour >= 7 && hour < 12) {
        m_session = SessionType::LONDON;
        m_bias = 1.0;
    } else if (hour >= 12 && hour < 16) {
        m_session = SessionType::OVERLAP;
        m_bias = 1.5;
    } else {
        m_session = SessionType::NY;
        m_bias = 1.0;
    }

    return m_session;
}

double SessionBias::bias() const {
    return m_bias;
}

SessionType SessionBias::session() const {
    return m_session;
}

}
