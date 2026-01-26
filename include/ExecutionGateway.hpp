#pragma once

#include "TradingGate.hpp"
#include "OrderIntent.hpp"
#include "BinanceRestClient.hpp"

#include <atomic>
#include <string>
#include <iostream>

class ExecutionGateway {
public:
    ExecutionGateway(TradingGate& gate,
                     BinanceRestClient& rest)
        : gate_(gate),
          rest_(rest),
          blocked_count_(0),
          sent_count_(0) {}

    // Returns true if order was SENT, false if BLOCKED
    bool send(const OrderIntent& intent, std::string& result_out) {
        if (!gate_.is_enabled()) {
            blocked_count_++;
            result_out = "BLOCKED: " + gate_.last_reason();
            std::cerr << "[EXEC_GATE] BLOCKED "
                      << intent.symbol << " "
                      << intent.side
                      << " qty=" << intent.quantity
                      << " reason=" << gate_.last_reason()
                      << "\n";
            return false;
        }

        try {
            std::string res;
            if (intent.is_market) {
                res = rest_.place_market_order(
                    intent.symbol,
                    intent.side,
                    intent.quantity
                );
            } else {
                res = rest_.place_limit_order(
                    intent.symbol,
                    intent.side,
                    intent.quantity,
                    intent.price
                );
            }

            sent_count_++;
            result_out = res;
            std::cout << "[EXEC_GATE] SENT "
                      << intent.symbol << " "
                      << intent.side
                      << " qty=" << intent.quantity
                      << "\n";
            return true;
        } catch (const std::exception& e) {
            result_out = std::string("ERROR: ") + e.what();
            std::cerr << "[EXEC_GATE] ERROR " << e.what() << "\n";
            return false;
        }
    }

    uint64_t blocked_count() const {
        return blocked_count_.load();
    }
    
    uint64_t sent_count() const {
        return sent_count_.load();
    }

private:
    TradingGate& gate_;
    BinanceRestClient& rest_;
    std::atomic<uint64_t> blocked_count_;
    std::atomic<uint64_t> sent_count_;
};
