#include "accounting/DailyPnlStore.hpp"

#include <fstream>
#include <ctime>
#include <iomanip>
#include <sstream>

DailyPnlStore::DailyPnlStore(const std::string& path)
    : path_(path) {}

std::string DailyPnlStore::today_ymd() const {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d");
    return oss.str();
}

double DailyPnlStore::load() {
    std::ifstream in(path_);
    if (!in.is_open()) {
        return 0.0;
    }

    std::string file_day;
    double pnl = 0.0;

    in >> file_day;
    in >> pnl;

    if (file_day != today_ymd()) {
        return 0.0;
    }

    return pnl;
}

void DailyPnlStore::save(double pnl) {
    std::ofstream out(path_, std::ios::trunc);
    out << today_ymd() << "\n";
    out << pnl << "\n";
}
