#pragma once
#include <string>
#include <cstdint>

namespace chimera {

// Minimal neutral FIX interface — adapt to your existing FIX session
class FixAdapter {
public:
    virtual ~FixAdapter() {}

    virtual void send_new_order(const std::string& symbol,
                                const std::string& side,   // "BUY" / "SELL"
                                double price,
                                double notional_usd,
                                bool post_only,
                                uint64_t client_order_id,
                                uint64_t send_ts_ns) = 0;
};

}
