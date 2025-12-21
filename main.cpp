#include "binance/BinanceSupervisor.hpp"
#include "binance/BinanceRestClient.hpp"
#include "cfd/CfdEngine.hpp"
#include "accounting/PnlLedger.hpp"
#include "accounting/DailyPnlStore.hpp"
#include "gui/MetricsHttpServer.hpp"
#include "latency/LatencyTracker.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <thread>
#include <iostream>
#include <filesystem>

static std::atomic<bool> g_running{true};

static void handle_signal(int) {
    g_running.store(false);
}

int main() {
    using clock = std::chrono::steady_clock;

    auto start_time = clock::now();

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    const double DAILY_LOSS_LIMIT_NZD = -200.0;

    std::filesystem::create_directories("state");

    PnlLedger ledger;
    DailyPnlStore daily_store("state/pnl_state.txt");

    LatencyTracker crypto_latency;
    LatencyTracker cfd_latency;
    LatencyTracker metrics_latency;

    double restored = daily_store.load();
    if (restored != 0.0) {
        ledger.record("RESTORE", restored);
    }

    binance::BinanceRestClient rest;
    binance::BinanceSupervisor supervisor(
        rest,
        "logs",
        9102,
        "binance"
    );

    supervisor.set_pnl_callback(
        [&](const std::string& source, double pnl) {
            auto t0 = clock::now();
            ledger.record("CRYPTO_" + source, pnl);
            auto t1 = clock::now();
            crypto_latency.observe_ns(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()
            );
        }
    );

    cfd::CfdEngine cfd;
    cfd.set_pnl_callback(
        [&](const std::string& tag, double pnl) {
            auto t0 = clock::now();
            ledger.record("CFD_" + tag, pnl);
            auto t1 = clock::now();
            cfd_latency.observe_ns(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()
            );
        }
    );
    cfd.start();

    MetricsHttpServer gui(8080);
    gui.start();

    while (g_running.load()) {
        auto m0 = clock::now();

        double total_pnl = ledger.total_nzd();
        daily_store.save(total_pnl);

        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
            clock::now() - start_time
        ).count();

        std::ofstream out("metrics_out/metrics.txt", std::ios::trunc);

        out << "chimera_pnl_nzd " << total_pnl << "\n";
        out << "chimera_daily_loss_limit_nzd " << DAILY_LOSS_LIMIT_NZD << "\n";

        out << "chimera_latency_crypto_ns " << crypto_latency.last() << "\n";
        out << "chimera_latency_cfd_ns " << cfd_latency.last() << "\n";
        out << "chimera_latency_metrics_ns "
            << std::chrono::duration_cast<std::chrono::nanoseconds>(
                   clock::now() - m0
               ).count()
            << "\n";

        auto snap = ledger.snapshot();
        for (const auto& kv : snap) {
            out << "chimera_pnl_strategy_" << kv.first << " " << kv.second << "\n";
        }

        out << "chimera_uptime_seconds " << uptime << "\n";
        out.close();

        if (total_pnl <= DAILY_LOSS_LIMIT_NZD) {
            g_running.store(false);
            break;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    gui.stop();
    cfd.stop(
        ledger.total_nzd() <= DAILY_LOSS_LIMIT_NZD
            ? cfd::KillReason::RISK_LIMIT
            : cfd::KillReason::NONE
    );

    daily_store.save(ledger.total_nzd());

    return 0;
}
