#pragma once
#include "../core/SymbolState.hpp"
#include "../supervision/V2Proposal.hpp"

namespace ChimeraV2 {

class IEngine {
public:
    virtual ~IEngine() = default;

    virtual int id() const = 0;
    virtual const char* name() const = 0;

    virtual V2Proposal evaluate(const SymbolState& state) = 0;

    virtual void on_trade_closed(double pnl, double R) = 0;
};

}
