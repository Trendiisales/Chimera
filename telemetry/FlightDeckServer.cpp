#include "external/httplib.h"
#include "TelemetryBus.hpp"
#include <sstream>

static std::string json_escape(const std::string& s) {
    std::ostringstream o;
    for (auto c : s) {
        switch (c) {
        case '"': o << "\\\""; break;
        case '\\': o << "\\\\"; break;
        case '\n': o << "\\n"; break;
        case '\r': o << "\\r"; break;
        case '\t': o << "\\t"; break;
        default: o << c; break;
        }
    }
    return o.str();
}

static std::string to_json() {
    auto events = TelemetryBus::instance().snapshot();
    std::ostringstream o;
    o << "[";

    for (size_t i = 0; i < events.size(); ++i) {
        auto& e = events[i];
        o << "{";
        o << "\"type\":\"" << json_escape(e.type) << "\",";
        o << "\"ts\":" << e.ts << ",";
        o << "\"fields\":{";

        size_t c = 0;
        for (auto& f : e.fields) {
            o << "\"" << json_escape(f.first) << "\":"
              << "\"" << json_escape(f.second) << "\"";
            if (++c < e.fields.size()) o << ",";
        }

        o << "}}";
        if (i + 1 < events.size()) o << ",";
    }

    o << "]";
    return o.str();
}

void startTelemetry(int port = 9090) {
    static httplib::Server svr;

    svr.Get("/snapshot", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(to_json(), "application/json");
    });

    std::thread([port]() {
        svr.listen("0.0.0.0", port);
    }).detach();
}
