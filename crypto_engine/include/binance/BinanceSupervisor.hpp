#pragma once

#include "binance/BinanceRestClient.hpp"
#include "binance/BinaryLogWriter.hpp"

#include <string>
#include <functional>

namespace binance {

class BinanceSupervisor {
public:
    using PnlCallback = BinaryLogWriter::PnlCallback;

    BinanceSupervisor(
        BinanceRestClient& rest,
        const std::string& log_dir,
        int metrics_port,
        const std::string& venue
    );

    // NEW: register PnL callback for all writers
    void set_pnl_callback(PnlCallback cb);

    // existing behavior unchanged

private:
    BinanceRestClient& rest_;
    std::string log_dir_;
    int metrics_port_;
    std::string venue_;

    // internal writers live here (already existed in cpp)
};

} // namespace binance
