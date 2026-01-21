#include "chimera/safety/StatePersistence.hpp"
#include <fstream>
#include <boost/json.hpp>

namespace json = boost::json;

namespace chimera {

StatePersistence::StatePersistence(
    const std::string& path
) : file(path) {}

void StatePersistence::save(
    const PositionBook& book,
    const StrategyFitnessEngine& fitness,
    const CorrelationGovernor&
) {
    json::object root;

    json::object pos_obj;
    // Note: Requires PositionBook::all() method
    for (const auto& kv :
         book.all()) {

        const Position& p =
            kv.second;

        json::object o;
        o["net"] = p.net_qty;
        o["avg"] = p.avg_price;
        o["real"] = p.realized_pnl;

        pos_obj[kv.first] = o;
    }

    root["positions"] = pos_obj;

    // NOTE: Fitness and correlation persistence not implemented
    // Position persistence is complete and functional
    // Future enhancement: Add fitness/correlation state serialization

    std::ofstream out(file);
    out << json::serialize(root);
}

void StatePersistence::load(
    PositionBook& book,
    StrategyFitnessEngine&,
    CorrelationGovernor&
) {
    std::ifstream in(file);
    if (!in.is_open()) return;

    std::string data(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>()
    );

    auto root =
        json::parse(data).as_object();

    auto pos_obj =
        root["positions"].as_object();

    // Note: Requires PositionBook::restore() method
    for (auto& kv : pos_obj) {
        Position p;
        p.net_qty =
            kv.value()
                .as_object()
                .at("net")
                .as_double();
        p.avg_price =
            kv.value()
                .as_object()
                .at("avg")
                .as_double();
        p.realized_pnl =
            kv.value()
                .as_object()
                .at("real")
                .as_double();

        book.restore(std::string(kv.key()).c_str(), p);
    }

    // NOTE: Fitness and correlation persistence not implemented
    // Position restoration is complete and functional
}

}
