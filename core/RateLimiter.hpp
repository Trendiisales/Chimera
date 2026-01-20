#pragma once

class RateLimiter {
public:
    RateLimiter(int = 0, int = 0) {}

    bool allow() const { return true; }
    int tokens() const { return 999; }
};
