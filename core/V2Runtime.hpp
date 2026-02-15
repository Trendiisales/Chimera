#pragma once
#include <vector>
#include <unordered_map>
#include "../engines/IEngine.hpp"
#include "../supervision/Supervisor.hpp"
#include "../risk/CapitalGovernor.hpp"
#include "../risk/EngineRiskTracker.hpp"
#include "../execution/ExecutionAuthority.hpp"
#include "../execution/PositionManager.hpp"
#include "../risk/PortfolioRiskState.hpp"
#include "../analytics/ExpectancyManager.hpp"
#include "SymbolRegistry.hpp"
#include "MarketStateBuilder.hpp"

namespace ChimeraV2 {

class V2Runtime {
public:
    V2Runtime()
        : supervisor_(&expectancy_) {}

    void register_engine(IEngine* engine) {
        engines_.push_back(engine);
        engine_map_[engine->id()] = engine;
    }

    void on_market(const std::string& symbol,
                   double bid,
                   double ask,
                   uint64_t now_ns) {

        SymbolState* state = registry_.find(symbol);
        if (!state) return;

        builder_.update(*state, bid, ask, now_ns);
        positions_.update_price(symbol, state->mid);

        exec_.manage(positions_, portfolioState_, engine_map_, now_ns);

        if (positions_.symbol_recent_stop(symbol, now_ns))
            return;

        std::vector<V2Proposal> proposals;

        for (auto* engine : engines_) {
            std::string key = state->symbol + std::to_string(engine->id());
            
            if (!engineRisk_.allowed(key, now_ns))
                continue;

            auto p = engine->evaluate(*state);
            if (p.valid)
                proposals.push_back(p);
        }

        auto best = supervisor_.select(proposals);

        if (best.valid &&
            governor_.approve(portfolioState_,
                positions_.total_open(),
                positions_.symbol_open(best.symbol))) {

            double stop_dist = best.estimated_risk;  // V1 PROVEN: Fixed per symbol

            Position pos;
            pos.symbol = best.symbol;
            pos.engine_id = best.engine_id;
            pos.side = best.side;
            pos.size = V2Config::LOT_SIZE;
            pos.entry_price = state->mid;

            // V1 PROVEN: Fixed stop distance and 2R target
            if (best.side == Side::BUY) {
                pos.stop_price = state->mid - stop_dist;
                pos.target_price = state->mid + (V2Config::TARGET_R_MULTIPLE * stop_dist);
            } else {
                pos.stop_price = state->mid + stop_dist;
                pos.target_price = state->mid - (V2Config::TARGET_R_MULTIPLE * stop_dist);
            }

            pos.entry_time_ns = now_ns;
            pos.max_hold_ns = V2Config::MAX_HOLD_SECONDS * 1000000000ULL;

            positions_.add(pos);
        }
    }

    const PositionManager& positions() const { return positions_; }
    const PortfolioRiskState& portfolio_state() const { return portfolioState_; }

private:
    std::vector<IEngine*> engines_;
    std::unordered_map<int, IEngine*> engine_map_;

    ExpectancyManager expectancy_;
    Supervisor supervisor_;
    CapitalGovernor governor_;
    EngineRiskTracker engineRisk_;
    ExecutionAuthority exec_;
    PositionManager positions_;
    PortfolioRiskState portfolioState_;
    SymbolRegistry registry_;
    MarketStateBuilder builder_;
};

}
