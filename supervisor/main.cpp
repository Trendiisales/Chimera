#include <iostream>
#include <thread>
#include <unordered_map>
#include <atomic>
#include <chrono>
#include <vector>
#include <mutex>
#include <sstream>
#include <fstream>
#include <csignal>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sched.h>

#include "chimera/Desk.hpp"

static const char* SOCK_PATH = "/tmp/chimera.sock";
static const char* SHM_NAME = "/chimera_pnl";
static const char* CONFIG_FILE = "/opt/chimera/config/risk.conf";
static const int HTTP_PORT = 9001;
static const int HEARTBEAT_TIMEOUT = 5;

struct EngineState {
    std::string name;
    pid_t pid = -1;
    double pnl = 0.0;
    std::chrono::steady_clock::time_point last_beat;
    bool alive = false;
    bool killed = false;
};

static std::unordered_map<std::string, EngineState> g_engines;
static std::mutex g_lock;
static std::atomic<bool> g_running{true};
static double g_daily_loss_limit = -500.0;

double* g_shm_pnl = nullptr;
Blotter* g_blotter = nullptr;

void load_config() {
    std::ifstream f(CONFIG_FILE);
    if (f.good()) f >> g_daily_loss_limit;
}

void pin_cpu(int core) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    sched_setaffinity(0, sizeof(set), &set);
}

void spawn(EngineState& e, int core) {
    pid_t pid = fork();
    if (pid == 0) {
        pin_cpu(core);
        execl(e.name.c_str(), e.name.c_str(), nullptr);
        std::exit(1);
    }
    e.pid = pid;
    e.alive = true;
    e.killed = false;
    e.last_beat = std::chrono::steady_clock::now();
    std::cout << "[SUPERVISOR] Spawned " << e.name << " PID=" << pid << " CPU=" << core << "\n";
}

void ipc_server() {
    unlink(SOCK_PATH);
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", SOCK_PATH);
    bind(fd, (sockaddr*)&addr, sizeof(addr));

    char buf[256];
    while (g_running) {
        int n = recv(fd, buf, sizeof(buf)-1, 0);
        if (n <= 0) continue;
        buf[n] = 0;
        std::string msg(buf);
        auto p = msg.find(":");
        if (p == std::string::npos) continue;

        std::string name = msg.substr(0, p);
        double pnl = std::stod(msg.substr(p+1));

        std::lock_guard<std::mutex> lock(g_lock);
        auto& e = g_engines[name];
        e.pnl = pnl;
        e.last_beat = std::chrono::steady_clock::now();
        e.alive = true;
    }
}

void monitor_loop() {
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        load_config();

        std::lock_guard<std::mutex> lock(g_lock);
        double total = 0;
        for (auto& it : g_engines) total += it.second.pnl;
        *g_shm_pnl = total;

        if (total <= g_daily_loss_limit) {
            std::cout << "[SUPERVISOR] DAILY LOSS LIMIT HIT\n";
            for (auto& it : g_engines) {
                if (it.second.pid > 0) kill(it.second.pid, SIGKILL);
                it.second.killed = true;
            }
            g_running = false;
            return;
        }

        for (auto& it : g_engines) {
            auto& e = it.second;
            if (e.killed || e.pid <= 0) continue;

            int status = 0;
            pid_t r = waitpid(e.pid, &status, WNOHANG);
            if (r == e.pid) {
                spawn(e, rand() % 4);
                continue;
            }

            auto dt = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - e.last_beat
            ).count();

            if (dt > HEARTBEAT_TIMEOUT) {
                kill(e.pid, SIGKILL);
                spawn(e, rand() % 4);
            }
        }
    }
}

void send_http(int c, const std::string& body) {
    std::ostringstream r;
    r << "HTTP/1.1 200 OK\r\n";
    r << "Content-Type: application/json\r\n";
    r << "Content-Length: " << body.size() << "\r\n\r\n";
    r << body;
    auto out = r.str();
    send(c, out.c_str(), out.size(), 0);
}

void http_server() {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(HTTP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(s, (sockaddr*)&addr, sizeof(addr));
    listen(s, 5);

    while (g_running) {
        int c = accept(s, nullptr, nullptr);
        if (c < 0) continue;

        std::ostringstream json;
        json << "{ \"total_pnl\": " << *g_shm_pnl << ", \"engines\": [";

        bool first = true;
        std::lock_guard<std::mutex> lock(g_lock);
        for (auto& it : g_engines) {
            if (!first) json << ",";
            first = false;
            auto& e = it.second;
            json << "{";
            json << "\"name\":\"" << e.name << "\",";
            json << "\"pid\":" << e.pid << ",";
            json << "\"pnl\":" << e.pnl << ",";
            json << "\"alive\":" << (e.alive ? "true" : "false") << ",";
            json << "\"killed\":" << (e.killed ? "true" : "false");
            json << "}";
        }
        json << "], \"orders\": [";

        uint32_t head = g_blotter->head;
        uint32_t start = head > 20 ? head - 20 : 0;
        for (uint32_t i = start; i < head; ++i) {
            auto& o = g_blotter->orders[i % MAX_ORDERS];
            if (i != start) json << ",";
            json << "{";
            json << "\"engine\":\"" << o.engine << "\",";
            json << "\"symbol\":\"" << o.symbol << "\",";
            json << "\"price\":" << o.price << ",";
            json << "\"qty\":" << o.qty << ",";
            json << "\"latency_us\":" << o.latency_us;
            json << "}";
        }
        json << "] }";

        send_http(c, json.str());
        close(c);
    }
}

int main() {
    signal(SIGINT, [](int){ g_running = false; });

    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, sizeof(double));
    g_shm_pnl = (double*)mmap(0, sizeof(double), PROT_READ|PROT_WRITE, MAP_SHARED, shm_fd, 0);
    *g_shm_pnl = 0;

    int bfd = shm_open(SHM_BLOTTER, O_CREAT | O_RDWR, 0666);
    ftruncate(bfd, sizeof(Blotter));
    g_blotter = (Blotter*)mmap(0, sizeof(Blotter), PROT_READ|PROT_WRITE, MAP_SHARED, bfd, 0);
    std::memset(g_blotter, 0, sizeof(Blotter));

    std::vector<std::string> engines = {
        "./chimera_crypto",
        "./chimera_gold",
        "./chimera_indices"
    };

    for (auto& n : engines) g_engines[n] = EngineState{n};

    int core = 0;
    for (auto& it : g_engines) spawn(it.second, core++);

    std::thread ipc(ipc_server);
    std::thread mon(monitor_loop);
    std::thread http(http_server);

    ipc.join();
    mon.join();
    http.join();
    return 0;
}
