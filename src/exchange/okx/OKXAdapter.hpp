#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <memory>
#include "exchange/VenueAdapter.hpp"
#include "exchange/okx/OKXAuth.hpp"
#include "exchange/okx/OKXRestClient.hpp"
#include "runtime/Context.hpp"

namespace chimera {

class OKXAdapter : public VenueAdapter {
public:
    // Context& injected for forensic recorder and future queue model wiring.
    // Credentials loaded from env at construction — see env var list below.
    OKXAdapter(Context& ctx, const std::string& rest, const std::string& ws);

    std::string name() const override;
    void run_market(std::atomic<bool>& running) override;
    void run_user(std::atomic<bool>& running)   override;
    bool send_order(const VenueOrder& ord)      override;
    bool cancel_order(const std::string& client_id) override;

    bool get_all_positions(std::vector<VenuePosition>& out)   override;
    bool get_all_open_orders(std::vector<VenueOpenOrder>& out) override;

private:
    // ---------------------------------------------------------------------------
    // Symbol mapping: internal Binance convention <-> OKX perpetual swap instId
    //   BTCUSDT  <->  BTC-USDT-SWAP
    //   ETHUSDT  <->  ETH-USDT-SWAP
    //   SOLUSDT  <->  SOL-USDT-SWAP
    // Fallback: XXXUSDT -> XXX-USDT-SWAP (generic parse)
    // ---------------------------------------------------------------------------
    static std::string to_okx_symbol(const std::string& internal_sym);
    static std::string from_okx_symbol(const std::string& okx_sym);

    void market_connect_loop(std::atomic<bool>& running);
    void parse_ticker(const std::string& msg);

    Context&    ctx_;
    std::string rest_base_;
    std::string ws_base_;

    std::unique_ptr<OKXAuth>       auth_;
    std::unique_ptr<OKXRestClient> rest_;
    bool has_credentials_{false};
};

} // namespace chimera
