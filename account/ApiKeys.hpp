#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>

struct ApiKeys {
    std::string api_key;
    std::string api_secret;
    bool dry_run = true;

    static ApiKeys load(const std::string& path) {
        ApiKeys k;
        std::ifstream f(path);
        if (!f.is_open()) throw std::runtime_error("Cannot open keys.json");

        std::stringstream ss;
        ss << f.rdbuf();
        std::string s = ss.str();

        auto get = [&](const std::string& key) {
            auto p = s.find(key);
            if (p == std::string::npos) return std::string();
            auto q = s.find('"', p + key.size() + 2);
            auto r = s.find('"', q + 1);
            return s.substr(q + 1, r - q - 1);
        };

        k.api_key = get("api_key");
        k.api_secret = get("api_secret");
        k.dry_run = get("mode") != "LIVE";
        return k;
    }
};
