#include "chimera/governance/SessionGovernor.hpp"

namespace chimera {

SessionGovernor::SessionGovernor()
    : eth_locked_(false), trading_allowed_(true) {}

void SessionGovernor::lockETH(bool v) {
    eth_locked_ = v;
}

bool SessionGovernor::ethLocked() const {
    return eth_locked_;
}

void SessionGovernor::setTradingAllowed(bool v) {
    trading_allowed_ = v;
}

bool SessionGovernor::tradingAllowed() const {
    return trading_allowed_;
}

} // namespace chimera
