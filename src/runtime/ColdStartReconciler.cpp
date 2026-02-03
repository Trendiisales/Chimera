#include "runtime/ColdStartReconciler.hpp"
#include <sstream>
#include <cmath>

using namespace chimera;

static bool approx_equal(double a, double b) {
    return std::fabs(a - b) < 1e-8;
}

ColdStartReconciler::ColdStartReconciler(Context& ctx)
    : ctx_(ctx) {}

bool ColdStartReconciler::reconcile(const std::vector<VenueAdapter*>& venues) {
    std::ostringstream out;
    bool ok = true;

    out << "[RECON] Cold-start reconciliation\n";

    // Pull exchange truth from all venues
    std::vector<VenuePosition> ex_positions;
    std::vector<VenueOpenOrder> ex_orders;

    for (auto* v : venues) {
        out << "[RECON] " << v->name() << " ... ";

        std::vector<VenuePosition> vp;
        if (!v->get_all_positions(vp)) {
            out << "FAIL (positions)\n";
            ok = false;
            continue;
        }
        for (auto& p : vp) ex_positions.push_back(p);

        std::vector<VenueOpenOrder> vo;
        if (!v->get_all_open_orders(vo)) {
            out << "FAIL (orders)\n";
            ok = false;
            continue;
        }
        for (auto& o : vo) ex_orders.push_back(o);

        out << "OK (" << vp.size() << " pos, " << vo.size() << " ord)\n";
    }

    // Compare exchange positions vs local snapshot
    auto local_pos = ctx_.risk.dump_positions();

    out << "[RECON] Comparing " << ex_positions.size()
        << " exchange vs " << local_pos.size() << " local positions\n";

    // Check: everything exchange has, local must agree on
    for (auto& p : ex_positions) {
        auto it = local_pos.find(p.symbol);
        if (it == local_pos.end()) {
            out << "[MISMATCH] " << p.symbol << " on exchange (qty=" << p.qty
                << ") missing locally\n";
            ok = false;
        } else if (!approx_equal(it->second, p.qty)) {
            out << "[MISMATCH] " << p.symbol
                << " local=" << it->second << " exchange=" << p.qty << "\n";
            ok = false;
        }
    }

    // Check: local positions with non-zero qty must exist on exchange
    for (auto& kv : local_pos) {
        if (std::fabs(kv.second) < 1e-8) continue; // zero is fine
        bool found = false;
        for (auto& p : ex_positions) {
            if (p.symbol == kv.first) { found = true; break; }
        }
        if (!found) {
            out << "[MISMATCH] " << kv.first << " local qty=" << kv.second
                << " not on exchange\n";
            ok = false;
        }
    }

    // Shadow mode: no open orders should exist on exchange
    if (!ex_orders.empty()) {
        out << "[MISMATCH] " << ex_orders.size()
            << " open orders on exchange — shadow mode should be clean\n";
        ok = false;
    }

    if (ok) {
        out << "[RECON] PASS — state aligned. Arm sequence unlocked.\n";
    } else {
        out << "[RECON] FAIL — arm system LOCKED. Manual intervention required.\n";
    }

    report_ = out.str();
    return ok;
}

std::string ColdStartReconciler::report() const {
    return report_;
}
