#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace chimera {

struct CoalesceOrder {
    std::string symbol;
    std::string engine_id;  // originating strategy — used by PnLGovernor gate
    double price;
    double qty;
};

class CancelReplaceCoalescer {
public:
    bool submit(const std::string& client_id, const CoalesceOrder& ord);
    bool get(const std::string& client_id, CoalesceOrder& out);
    void clear(const std::string& client_id);

    // ---------------------------------------------------------------------------
    // Find an existing pending order from the same engine on the same symbol.
    // Returns true + populates out_client_id + out_ord if found.
    // Used by ExecutionRouter to deduplicate / cancel-replace before inserting.
    // ---------------------------------------------------------------------------
    bool find_by_engine_symbol(const std::string& engine_id,
                               const std::string& symbol,
                               std::string& out_client_id,
                               CoalesceOrder& out_ord);

    // FIX 2.1: Expose pending order keys so ExecutionRouter::poll() can
    // iterate and process them in shadow mode.
    // Previously: coalescer had no iteration — poll() couldn't drain pending orders.
    std::vector<std::string> pending_keys();

private:
    std::unordered_map<std::string, CoalesceOrder> pending_;
    std::mutex mtx_;
};

}
