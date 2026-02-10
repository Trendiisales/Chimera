#pragma once
#include <cstdint>

struct SessionGuardConfig {
    uint32_t session_close_utc;
    uint32_t flatten_buffer_sec;
    uint32_t liquidity_fade_sec;
};

class SessionGuard {
public:
    explicit SessionGuard(const SessionGuardConfig& cfg) : cfg_(cfg) {}
    
    bool allow_new_trade(uint32_t now_utc_sec) const {
        return now_utc_sec + cfg_.flatten_buffer_sec + cfg_.liquidity_fade_sec < cfg_.session_close_utc;
    }
    
    bool must_flatten(uint32_t now_utc_sec) const {
        return now_utc_sec >= cfg_.session_close_utc - cfg_.flatten_buffer_sec;
    }
    
private:
    SessionGuardConfig cfg_;
};
