#include "chimera/strategy/ETHFade.hpp"

namespace chimera {

ETHFade::ETHFade(
    ExecutionEngine& exec,
    Microstructure& micro
) : execution(exec),
    microstructure(micro) {}

void ETHFade::onTick(
    const std::string& symbol,
    double bid,
    double ask,
    double spread,
    uint64_t
) {
    (void)bid;
    if (symbol != "ETHUSDT") return;
    if (spread > max_spread) return;

    double ofi = microstructure.ofi(symbol);
    bool impulse = microstructure.impulseOpen(symbol);

    if (impulse) return;

    TradeSignal sig;
    sig.engine = "ETH_FADE";
    sig.symbol = symbol;
    sig.qty = order_size;
    sig.price = ask;

    if (ofi > fade_ofi_threshold) {
        sig.is_buy = false;
        execution.onSignal(sig);
    } else if (ofi < -fade_ofi_threshold) {
        sig.is_buy = true;
        execution.onSignal(sig);
    }
}

}
