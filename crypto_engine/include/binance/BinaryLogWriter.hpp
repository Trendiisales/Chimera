#pragma once

#include <functional>
#include <string>

namespace binance {

class BinaryLogWriter {
public:
    using PnlCallback = std::function<void(const std::string& source, double pnl_nzd)>;

    // EXISTING constructor (DO NOT BREAK)
    explicit BinaryLogWriter(const std::string& path);

    // NEW: optional callback
    void set_pnl_callback(PnlCallback cb);

    // EXISTING API (unchanged)
    void write_trade(
        const std::string& symbol,
        double qty,
        double price,
        double realized_pnl_nzd
    );

private:
    std::string path_;
    PnlCallback pnl_cb_;
};

} // namespace binance
