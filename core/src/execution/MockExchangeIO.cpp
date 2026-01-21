#include "chimera/execution/ExchangeIO.hpp"
#include <chrono>
#include <thread>

namespace chimera {

class MockExchangeIO : public IExchangeIO {
public:
    void connect() override {}
    void disconnect() override {}

    void subscribeMarketData(
        const std::vector<std::string>& syms
    ) override {
        symbols = syms;
    }

    void sendOrder(const OrderRequest& req) override {
        OrderUpdate up;
        up.client_id = req.client_id;
        up.exchange_id = "MOCK-" + req.client_id;
        up.filled_qty = req.qty;
        up.avg_price = req.price;
        up.is_final = true;
        up.status = "FILLED";

        if (on_order_update) {
            on_order_update(up);
        }
    }

    void cancelOrder(const std::string&) override {}

    void poll() override {
        for (const auto& s : symbols) {
            MarketTick t;
            t.symbol = s;
            t.bid = 100.0;
            t.ask = 100.1;
            t.last = 100.05;
            t.bid_size = 1.0;
            t.ask_size = 1.0;
            t.ts_ns = nowNs();

            if (on_tick) {
                on_tick(t);
            }
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(100)
        );
    }

private:
    std::vector<std::string> symbols;

    uint64_t nowNs() {
        return std::chrono::duration_cast<
            std::chrono::nanoseconds
        >(
            std::chrono::steady_clock::now()
            .time_since_epoch()
        ).count();
    }
};

}
