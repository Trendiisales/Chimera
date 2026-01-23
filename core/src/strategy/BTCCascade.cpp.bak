#include "chimera/strategy/BTCCascade.hpp"

namespace chimera {

BTCCascade::BTCCascade(
    ExecutionEngine& exec,
    Microstructure& micro
) : execution(exec),
    microstructure(micro) {}

void BTCCascade::onTick(
    const std::string& symbol,
    double bid,
    double ask,
    double spread,
    uint64_t
) {
    if (symbol != "BTCUSDT") return;
    if (spread > max_spread) return;

    double ofi = microstructure.ofi(symbol);
    bool impulse = microstructure.impulseOpen(symbol);

    if (!impulse) return;

    TradeSignal sig;
    sig.engine = "BTC_CASCADE";
    sig.symbol = symbol;
    sig.qty = order_size;
    sig.price = ask;

    if (ofi > momentum_ofi_threshold) {
        sig.is_buy = true;
        execution.onSignal(sig);
    } else if (ofi < -momentum_ofi_threshold) {
        sig.is_buy = false;
        execution.onSignal(sig);
    }
}

}
