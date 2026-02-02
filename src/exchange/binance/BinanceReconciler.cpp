#include "exchange/binance/BinanceReconciler.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <cmath>

using json = nlohmann::json;
using namespace chimera;

BinanceReconciler::BinanceReconciler(BinanceRestClient& rest)
    : rest_(rest) {}

bool BinanceReconciler::reconcile() {
    report_ = "[BINANCE RECON]\n";

    // --- Fetch account state ---
    std::string acct_raw;
    try {
        acct_raw = rest_.get_account_snapshot();
    } catch (const std::exception& e) {
        report_ += "  ACCOUNT FETCH FAILED: " + std::string(e.what()) + "\n";
        return false;
    }

    // --- Fetch open orders ---
    std::string orders_raw;
    try {
        orders_raw = rest_.get_open_orders();
    } catch (const std::exception& e) {
        report_ += "  OPEN ORDERS FETCH FAILED: " + std::string(e.what()) + "\n";
        return false;
    }

    // --- Parse account positions ---
    bool clean = true;
    try {
        json acct = json::parse(acct_raw);

        if (acct.contains("positions") && acct["positions"].is_array()) {
            for (const auto& pos : acct["positions"]) {
                if (!pos.contains("positionAmt")) continue;

                std::string amt_str = pos["positionAmt"].is_string()
                    ? pos["positionAmt"].get<std::string>()
                    : std::to_string(pos["positionAmt"].get<double>());

                double amt = std::stod(amt_str);

                if (std::abs(amt) > 1e-8) {
                    std::string sym = pos.contains("symbol")
                        ? pos["symbol"].get<std::string>() : "UNKNOWN";
                    report_ += "  OPEN POSITION: " + sym +
                               " amt=" + amt_str + "\n";
                    clean = false;
                }
            }
        }
    } catch (const std::exception& e) {
        report_ += "  ACCOUNT PARSE FAILED: " + std::string(e.what()) + "\n";
        return false;
    }

    // --- Parse open orders ---
    try {
        json orders = json::parse(orders_raw);

        if (orders.is_array() && !orders.empty()) {
            report_ += "  OPEN ORDERS: " + std::to_string(orders.size()) + " present\n";
            for (const auto& ord : orders) {
                std::string sym   = ord.contains("symbol")  ? ord["symbol"].get<std::string>()  : "?";
                std::string side  = ord.contains("side")    ? ord["side"].get<std::string>()    : "?";
                std::string status= ord.contains("status")  ? ord["status"].get<std::string>()  : "?";
                report_ += "    " + sym + " " + side + " status=" + status + "\n";
            }
            clean = false;
        }
    } catch (const std::exception& e) {
        report_ += "  ORDERS PARSE FAILED: " + std::string(e.what()) + "\n";
        return false;
    }

    if (clean) {
        report_ += "  ALL CLEAR — no positions, no open orders\n";
    }

    return clean;
}

const std::string& BinanceReconciler::report() const {
    return report_;
}
