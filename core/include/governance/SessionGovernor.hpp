#pragma once

#include <atomic>

namespace chimera {

class SessionGovernor {
public:
    SessionGovernor();
    
    void lockETH(bool v);
    bool ethLocked() const;
    
    void setTradingAllowed(bool v);
    bool tradingAllowed() const;

private:
    std::atomic<bool> eth_locked_;
    std::atomic<bool> trading_allowed_;
};

} // namespace chimera
