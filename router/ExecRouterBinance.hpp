#pragma once
#include <string>
#include "../exchange/BinanceREST.hpp"
#include "../risk/KillSwitchGovernor.hpp"

class ExecRouterBinance {
public:
    ExecRouterBinance(BinanceREST* rest,
                      KillSwitchGovernor* kill,
                      const std::string& engine_name)
        : rest_(rest), kill_(kill), engine_(engine_name) {}

    void send(bool is_buy, double size, double price, const std::string& symbol) {
        if (!kill_->globalEnabled()) return;
        if (!kill_->isEngineEnabled(engine_)) return;

        double scaled = kill_->scaleSize(engine_, size);
        if (scaled <= 0.0) return;

        rest_->sendOrder(
            symbol,
            is_buy ? "BUY" : "SELL",
            scaled,
            price,
            true
        );

        kill_->recordSignal(engine_, 0);
    }

private:
    BinanceREST* rest_;
    KillSwitchGovernor* kill_;
    std::string engine_;
};
