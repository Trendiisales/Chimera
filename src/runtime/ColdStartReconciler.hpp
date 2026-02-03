#pragma once
#include <vector>
#include <string>
#include "runtime/Context.hpp"
#include "exchange/VenueAdapter.hpp"

namespace chimera {

// Gatekeeper before system can arm or trade.
// Pulls exchange-truth from every venue, compares to local snapshot.
// If mismatch: blocks. If clean: allows arm sequence to proceed.
class ColdStartReconciler {
public:
    explicit ColdStartReconciler(Context& ctx);

    bool reconcile(const std::vector<VenueAdapter*>& venues);
    std::string report() const;

private:
    Context& ctx_;
    std::string report_;
};

}
