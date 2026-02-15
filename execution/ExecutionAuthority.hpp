#pragma once
#include "PositionManager.hpp"
#include "../config/V2Config.hpp"
#include "../risk/PortfolioRiskState.hpp"
#include "../engines/IEngine.hpp"
#include <cmath>
#include <unordered_map>

namespace ChimeraV2 {

class ExecutionAuthority {
public:

    void manage(PositionManager& manager,
                PortfolioRiskState& portfolio,
                std::unordered_map<int, IEngine*>& engines,
                uint64_t now_ns) {

        for (auto& pos : manager.positions()) {
            if (!pos.open) continue;

            double current = manager.get_live_price(pos.symbol);
            if (current == 0.0) continue;

            double pnl = manager.compute_pnl(pos);

            double risk_unit = std::abs(pos.entry_price - pos.stop_price);
            double R = 0.0;
            
            if (risk_unit > 0.0) {
                double diff = (pos.side == Side::BUY)
                                ? (current - pos.entry_price)
                                : (pos.entry_price - current);
                R = diff / risk_unit;
            }

            bool exit = false;
            bool stopped = false;

            if ((pos.side == Side::BUY && current <= pos.stop_price) ||
                (pos.side == Side::SELL && current >= pos.stop_price)) {
                exit = true;
                stopped = true;
            }

            if ((pos.side == Side::BUY && current >= pos.target_price) ||
                (pos.side == Side::SELL && current <= pos.target_price)) {
                exit = true;
            }

            if (now_ns - pos.entry_time_ns >= pos.max_hold_ns) {
                exit = true;
            }

            if (R < V2Config::WEAK_TRADE_R_THRESHOLD &&
                now_ns - pos.entry_time_ns > (V2Config::WEAK_TRADE_EXIT_SECONDS * 1000000000ULL)) {
                exit = true;
            }

            if (exit) {
                pos.open = false;
                portfolio.daily_pnl += pnl;

                if (pnl < 0) {
                    portfolio.consecutive_losses++;
                    if (stopped) {
                        manager.record_stop(pos.symbol, now_ns);
                    }
                } else {
                    portfolio.consecutive_losses = 0;
                }

                auto it = engines.find(pos.engine_id);
                if (it != engines.end()) {
                    it->second->on_trade_closed(pnl, R);
                }
            }
        }

        if (portfolio.consecutive_losses >= V2Config::PORTFOLIO_MAX_CONSEC_LOSSES) {
            portfolio.portfolio_cooldown = true;
            portfolio.cooldown_end_ns =
                now_ns + V2Config::PORTFOLIO_COOLDOWN_SECONDS * 1000000000ULL;
        }

        if (portfolio.portfolio_cooldown &&
            now_ns >= portfolio.cooldown_end_ns) {
            portfolio.portfolio_cooldown = false;
            portfolio.consecutive_losses = 0;
        }
    }
};

}
