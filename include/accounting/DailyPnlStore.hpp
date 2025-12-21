#pragma once

#include <string>

class DailyPnlStore {
public:
    explicit DailyPnlStore(const std::string& path);

    // Load persisted pnl (or 0 if new day / missing)
    double load();

    // Persist pnl for today
    void save(double pnl);

private:
    std::string path_;

    std::string today_ymd() const;
};
