#pragma once
#include <string>
#include <vector>

namespace binance {

/*
 Build JSON subscription frame.
 Binance requires SUBSCRIBE message when not using URL streams.
*/
inline std::string subscribe_frame(const std::vector<std::string>& symbols, int id = 1) {
    std::string j = R"({"method":"SUBSCRIBE","params":[)";
    bool first = true;

    for (const auto& s : symbols) {
        if (!first)
            j += ",";
        first = false;
        j += "\"";
        for (char c : s)
            j.push_back(static_cast<char>(::tolower(c)));
        j += "@depth@100ms\"";
    }

    j += "],\"id\":";
    j += std::to_string(id);
    j += "}";

    return j;
}

inline std::string unsubscribe_frame(const std::vector<std::string>& symbols, int id = 2) {
    std::string j = R"({"method":"UNSUBSCRIBE","params":[)";
    bool first = true;

    for (const auto& s : symbols) {
        if (!first)
            j += ",";
        first = false;
        j += "\"";
        for (char c : s)
            j.push_back(static_cast<char>(::tolower(c)));
        j += "@depth@100ms\"";
    }

    j += "],\"id\":";
    j += std::to_string(id);
    j += "}";

    return j;
}

}
