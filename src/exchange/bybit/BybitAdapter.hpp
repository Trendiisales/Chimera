#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <memory>
#include "exchange/VenueAdapter.hpp"
#include "exchange/bybit/BybitAuth.hpp"
#include "exchange/bybit/BybitRestClient.hpp"
#include "runtime/Context.hpp"

namespace chimera {

class BybitAdapter : public VenueAdapter {
public:
    // Context& injected for forensic recorder.
    // Credentials loaded from env at construction.
    BybitAdapter(Context& ctx, const std::string& rest, const std::string& ws);

    std::string name() const override;
    void run_market(std::atomic<bool>& running) override;
    void run_user(std::atomic<bool>& running)   override;
    bool send_order(const VenueOrder& ord)      override;
    bool cancel_order(const std::string& client_id) override;

    bool get_all_positions(std::vector<VenuePosition>& out)   override;
    bool get_all_open_orders(std::vector<VenueOpenOrder>& out) override;

private:
    // Bybit linear perpetuals use same symbol format as Binance (BTCUSDT).
    // No mapping needed. Forensic log tags with "BBT:" prefix to distinguish.

    void market_connect_loop(std::atomic<bool>& running);
    void parse_ticker(const std::string& msg);

    Context&    ctx_;
    std::string rest_base_;
    std::string ws_base_;

    std::unique_ptr<BybitAuth>       auth_;
    std::unique_ptr<BybitRestClient> rest_;
    bool has_credentials_{false};
};

} // namespace chimera
