#include "binance/BinaryLogWriter.hpp"

#include <iostream>

namespace binance {

BinaryLogWriter::BinaryLogWriter(const std::string& path)
    : path_(path) {}

void BinaryLogWriter::set_pnl_callback(PnlCallback cb) {
    pnl_cb_ = std::move(cb);
}

void BinaryLogWriter::write_trade(
    const std::string& symbol,
    double qty,
    double price,
    double realized_pnl_nzd
) {
    // Existing behavior (placeholder for binary logging)
    std::cout << "[TRADE] " << symbol
              << " qty=" << qty
              << " price=" << price
              << " pnl=" << realized_pnl_nzd
              << " -> " << path_ << "\n";

    // NEW: emit realized PnL if hooked
    if (pnl_cb_) {
        pnl_cb_(symbol, realized_pnl_nzd);
    }
}

} // namespace binance
