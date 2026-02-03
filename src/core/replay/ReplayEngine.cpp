#include "replay/ReplayEngine.hpp"
#include <fstream>
#include <sstream>
#include <cctype>

namespace chimera {

// Extract a top-level JSON value by key (string or number)
static std::string extractValue(const std::string& src, const std::string& key) {
    std::string k = "\"" + key + "\":";
    auto pos = src.find(k);
    if (pos == std::string::npos) return "";
    pos += k.size();

    while (pos < src.size() && std::isspace(src[pos])) pos++;

    if (pos >= src.size()) return "";

    if (src[pos] == '"') {
        auto end = src.find('"', pos + 1);
        if (end == std::string::npos) return "";
        return src.substr(pos + 1, end - pos - 1);
    }

    auto end = src.find_first_of(",}", pos);
    if (end == std::string::npos) return "";
    return src.substr(pos, end - pos);
}

// Extract the JSON object assigned to "payload"
static std::string extractPayloadObject(const std::string& line) {
    std::string k = "\"payload\":";
    auto pos = line.find(k);
    if (pos == std::string::npos) return "";
    pos += k.size();

    while (pos < line.size() && std::isspace(line[pos])) pos++;

    if (pos >= line.size() || line[pos] != '{') return "";

    int depth = 0;
    for (size_t i = pos; i < line.size(); ++i) {
        if (line[i] == '{') depth++;
        else if (line[i] == '}') {
            depth--;
            if (depth == 0) {
                return line.substr(pos, i - pos + 1);
            }
        }
    }
    return "";
}

ReplayEngine::ReplayEngine(PositionState& ps)
    : m_positions(ps) {}

void ReplayEngine::loadJournal(const std::string& path) {
    m_events.clear();

    std::ifstream in(path + ".jsonl");
    std::string line;

    while (std::getline(in, line)) {
        ReplayEvent ev;
        ev.id = std::stoull(extractValue(line, "id"));
        ev.ts_ns = std::stoull(extractValue(line, "ts_ns"));
        ev.type = extractValue(line, "type");

        std::string payloadObj = extractPayloadObject(line);
        ev.payload = payloadObj;

        m_events.push_back(ev);
    }
}

void ReplayEngine::onEvent(const std::function<void(const ReplayEvent&)>& cb) {
    m_cb = cb;
}

void ReplayEngine::apply(const ReplayEvent& ev) {
    if (ev.type == "SHADOW_FILL") {
        std::string payload = ev.payload;

        std::string sym = extractValue(payload, "symbol");
        std::string eng = extractValue(payload, "engine");
        std::string price_s = extractValue(payload, "price");
        std::string qty_s = extractValue(payload, "qty");

        if (sym.empty() || eng.empty() || price_s.empty() || qty_s.empty()) {
            return;
        }

        double price = std::stod(price_s);
        double qty = std::stod(qty_s);

        m_positions.onFill(sym, eng, price, qty, 0.0, ev.id);
    }
}

void ReplayEngine::run() {
    for (const auto& ev : m_events) {
        apply(ev);
        if (m_cb) m_cb(ev);
    }
}

}
