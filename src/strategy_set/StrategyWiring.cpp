#include "strategy_multi/MultiStrategyCoordinator.hpp"

#include "strategy_set/NoTrade_Guard.hpp"
#include "strategy_set/OBI_Momentum.hpp"
#include "strategy_set/OBI_Reversion.hpp"
#include "strategy_set/Microprice_Trend.hpp"
#include "strategy_set/Microprice_Reversion.hpp"
#include "strategy_set/Flow_Momentum.hpp"
#include "strategy_set/Flow_Exhaustion.hpp"
#include "strategy_set/Vol_Expansion.hpp"
#include "strategy_set/Vol_Compression.hpp"
#include "strategy_set/Liquidity_Vacuum.hpp"

using namespace Chimera;

void wire_strategies(MultiStrategyCoordinator& coord) {
    coord.add(std::make_unique<NoTrade_Guard>());

    coord.add(std::make_unique<OBI_Momentum>());
    coord.add(std::make_unique<OBI_Reversion>());

    coord.add(std::make_unique<Microprice_Trend>());
    coord.add(std::make_unique<Microprice_Reversion>());

    coord.add(std::make_unique<Flow_Momentum>());
    coord.add(std::make_unique<Flow_Exhaustion>());

    coord.add(std::make_unique<Vol_Expansion>());
    coord.add(std::make_unique<Vol_Compression>());

    coord.add(std::make_unique<Liquidity_Vacuum>());
}
