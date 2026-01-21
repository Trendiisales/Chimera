#include "chimera/causal/SignalAttributionLedger.hpp"
#include <boost/json.hpp>

namespace json = boost::json;

namespace chimera {

void SignalAttributionLedger::recordTrade(const SignalAttribution& attr) {
    attributions_.push_back(attr);
}

const std::vector<SignalAttribution>& 
SignalAttributionLedger::getAttributions() const {
    return attributions_;
}

std::unordered_map<std::string, SignalAttributionLedger::SignalStats> 
SignalAttributionLedger::computeSignalStats() const {
    std::unordered_map<std::string, SignalStats> stats;
    
    // Aggregate OFI
    SignalStats& ofi = stats["ofi"];
    // Aggregate impulse
    SignalStats& impulse = stats["impulse"];
    // Aggregate spread
    SignalStats& spread = stats["spread"];
    // Aggregate depth
    SignalStats& depth = stats["depth"];
    // Aggregate toxic
    SignalStats& toxic = stats["toxic"];
    // Aggregate vpin
    SignalStats& vpin = stats["vpin"];
    // Aggregate regime
    SignalStats& regime = stats["regime"];
    // Aggregate funding
    SignalStats& funding = stats["funding"];
    
    for (const auto& attr : attributions_) {
        // OFI
        ofi.total_contrib_bps += attr.ofi_contrib_bps;
        ofi.trade_count++;
        if (attr.ofi_contrib_bps > 0) {
            ofi.positive_contrib_bps += attr.ofi_contrib_bps;
            ofi.positive_count++;
        } else if (attr.ofi_contrib_bps < 0) {
            ofi.negative_contrib_bps += attr.ofi_contrib_bps;
            ofi.negative_count++;
        }
        
        // Impulse
        impulse.total_contrib_bps += attr.impulse_contrib_bps;
        impulse.trade_count++;
        if (attr.impulse_contrib_bps > 0) {
            impulse.positive_contrib_bps += attr.impulse_contrib_bps;
            impulse.positive_count++;
        } else if (attr.impulse_contrib_bps < 0) {
            impulse.negative_contrib_bps += attr.impulse_contrib_bps;
            impulse.negative_count++;
        }
        
        // Spread
        spread.total_contrib_bps += attr.spread_contrib_bps;
        spread.trade_count++;
        if (attr.spread_contrib_bps > 0) {
            spread.positive_contrib_bps += attr.spread_contrib_bps;
            spread.positive_count++;
        } else if (attr.spread_contrib_bps < 0) {
            spread.negative_contrib_bps += attr.spread_contrib_bps;
            spread.negative_count++;
        }
        
        // Depth
        depth.total_contrib_bps += attr.depth_contrib_bps;
        depth.trade_count++;
        if (attr.depth_contrib_bps > 0) {
            depth.positive_contrib_bps += attr.depth_contrib_bps;
            depth.positive_count++;
        } else if (attr.depth_contrib_bps < 0) {
            depth.negative_contrib_bps += attr.depth_contrib_bps;
            depth.negative_count++;
        }
        
        // Toxic
        toxic.total_contrib_bps += attr.toxic_contrib_bps;
        toxic.trade_count++;
        if (attr.toxic_contrib_bps > 0) {
            toxic.positive_contrib_bps += attr.toxic_contrib_bps;
            toxic.positive_count++;
        } else if (attr.toxic_contrib_bps < 0) {
            toxic.negative_contrib_bps += attr.toxic_contrib_bps;
            toxic.negative_count++;
        }
        
        // VPIN
        vpin.total_contrib_bps += attr.vpin_contrib_bps;
        vpin.trade_count++;
        if (attr.vpin_contrib_bps > 0) {
            vpin.positive_contrib_bps += attr.vpin_contrib_bps;
            vpin.positive_count++;
        } else if (attr.vpin_contrib_bps < 0) {
            vpin.negative_contrib_bps += attr.vpin_contrib_bps;
            vpin.negative_count++;
        }
        
        // Regime
        regime.total_contrib_bps += attr.regime_contrib_bps;
        regime.trade_count++;
        if (attr.regime_contrib_bps > 0) {
            regime.positive_contrib_bps += attr.regime_contrib_bps;
            regime.positive_count++;
        } else if (attr.regime_contrib_bps < 0) {
            regime.negative_contrib_bps += attr.regime_contrib_bps;
            regime.negative_count++;
        }
        
        // Funding
        funding.total_contrib_bps += attr.funding_contrib_bps;
        funding.trade_count++;
        if (attr.funding_contrib_bps > 0) {
            funding.positive_contrib_bps += attr.funding_contrib_bps;
            funding.positive_count++;
        } else if (attr.funding_contrib_bps < 0) {
            funding.negative_contrib_bps += attr.funding_contrib_bps;
            funding.negative_count++;
        }
    }
    
    // Compute means
    for (auto& kv : stats) {
        if (kv.second.trade_count > 0) {
            kv.second.mean_contrib_bps = 
                kv.second.total_contrib_bps / kv.second.trade_count;
        }
    }
    
    return stats;
}

void SignalAttributionLedger::saveToDisk(const std::string& filepath) const {
    json::array arr;
    
    for (const auto& attr : attributions_) {
        json::object obj;
        obj["trade_id"] = attr.trade_id;
        obj["engine"] = attr.engine;
        obj["symbol"] = attr.symbol;
        obj["timestamp_ms"] = attr.timestamp_ms;
        obj["ofi_contrib_bps"] = attr.ofi_contrib_bps;
        obj["impulse_contrib_bps"] = attr.impulse_contrib_bps;
        obj["spread_contrib_bps"] = attr.spread_contrib_bps;
        obj["depth_contrib_bps"] = attr.depth_contrib_bps;
        obj["toxic_contrib_bps"] = attr.toxic_contrib_bps;
        obj["vpin_contrib_bps"] = attr.vpin_contrib_bps;
        obj["regime_contrib_bps"] = attr.regime_contrib_bps;
        obj["funding_contrib_bps"] = attr.funding_contrib_bps;
        obj["execution_slippage_bps"] = attr.execution_slippage_bps;
        obj["fee_drag_bps"] = attr.fee_drag_bps;
        obj["total_pnl_bps"] = attr.total_pnl_bps;
        
        arr.push_back(obj);
    }
    
    std::ofstream out(filepath);
    out << json::serialize(arr);
}

void SignalAttributionLedger::loadFromDisk(const std::string& filepath) {
    std::ifstream in(filepath);
    if (!in.is_open()) return;
    
    std::string data(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>()
    );
    
    auto arr = json::parse(data).as_array();
    
    for (const auto& e : arr) {
        auto obj = e.as_object();
        
        SignalAttribution attr;
        attr.trade_id = obj["trade_id"].as_string().c_str();
        attr.engine = obj["engine"].as_string().c_str();
        attr.symbol = obj["symbol"].as_string().c_str();
        attr.timestamp_ms = obj["timestamp_ms"].as_int64();
        attr.ofi_contrib_bps = obj["ofi_contrib_bps"].as_double();
        attr.impulse_contrib_bps = obj["impulse_contrib_bps"].as_double();
        attr.spread_contrib_bps = obj["spread_contrib_bps"].as_double();
        attr.depth_contrib_bps = obj["depth_contrib_bps"].as_double();
        attr.toxic_contrib_bps = obj["toxic_contrib_bps"].as_double();
        attr.vpin_contrib_bps = obj["vpin_contrib_bps"].as_double();
        attr.regime_contrib_bps = obj["regime_contrib_bps"].as_double();
        attr.funding_contrib_bps = obj["funding_contrib_bps"].as_double();
        attr.execution_slippage_bps = obj["execution_slippage_bps"].as_double();
        attr.fee_drag_bps = obj["fee_drag_bps"].as_double();
        attr.total_pnl_bps = obj["total_pnl_bps"].as_double();
        
        attributions_.push_back(attr);
    }
}

void SignalAttributionLedger::clear() {
    attributions_.clear();
}

}
