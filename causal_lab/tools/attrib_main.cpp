#include "ReplayEngine.hpp"
#include "ShadowFarm.hpp"
#include "AttributionEngine.hpp"
#include "RegimeStore.hpp"
#include <iostream>
#include <map>

using namespace chimera_lab;

class BaselineStrat : public ShadowStrategy {
public:
    std::string name() const override { return "BASELINE"; }

    bool decide(const SignalVector&, double, double& qty) override {
        qty = 1.0;
        return true;
    }

    double simulateFill(double price, double qty) override {
        return price * qty * 0.0001; // Assume 1 bps edge
    }
};

class NoOFIStrat : public BaselineStrat {
public:
    std::string name() const override { return "NO_OFI"; }
    
    double simulateFill(double price, double qty) override {
        return price * qty * 0.00005; // Half the edge without OFI
    }
};

class NoImpulseStrat : public BaselineStrat {
public:
    std::string name() const override { return "NO_IMPULSE"; }
    
    double simulateFill(double price, double qty) override {
        return price * qty * 0.00008; // Slight reduction
    }
};

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: chimera_attrib <event_log.bin> <out.csv>\n";
        return 1;
    }

    ReplayEngine replay(argv[1]);
    ShadowFarm farm;

    BaselineStrat base;
    NoOFIStrat no_ofi;
    NoImpulseStrat no_imp;

    farm.add(&base);
    farm.add(&no_ofi);
    farm.add(&no_imp);

    AttributionEngine attrib;
    RegimeStore store(argv[2]);

    replay.onFill([&](const EventHeader& h, const FillPayload& f) {
        SignalVector s{};
        double price = f.fill_price;

        auto results = farm.evaluate(h.event_id, s, price);

        std::map<std::string, double> baseline;
        std::map<std::string, double> no_ofi;
        std::map<std::string, double> no_imp;
        std::map<std::string, double> dummy;

        for (auto& r : results) {
            if (r.variant == "BASELINE") baseline["pnl"] = r.expected_pnl;
            if (r.variant == "NO_OFI") no_ofi["pnl"] = r.expected_pnl;
            if (r.variant == "NO_IMPULSE") no_imp["pnl"] = r.expected_pnl;
        }

        dummy["pnl"] = baseline["pnl"];

        AttributionResult ar = attrib.shapley(
            baseline, no_ofi, no_imp, dummy, dummy, dummy, dummy, dummy, dummy
        );

        store.write(h.event_id,
                    std::to_string(h.symbol_hash),
                    "UNKNOWN",
                    ar,
                    baseline["pnl"]);
    });

    replay.run();
    std::cout << "Attribution complete. Results saved to " << argv[2] << "\n";
    return 0;
}
